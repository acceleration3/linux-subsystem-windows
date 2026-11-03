#include "lsw/agent/win/pe_image.hpp"

#include <cstring>
#include <windows.h>

namespace lsw::agent::win {

namespace {
constexpr std::size_t kMaxImportModules = 4096;
constexpr std::size_t kMaxImportFunctions = 1 << 20;
constexpr std::size_t kMaxNameChars = 1024;
}

std::unique_ptr<PeImage> PeImage::open(const std::wstring& path) {
    std::unique_ptr<PeImage> image(new PeImage());
    image->file_.reset(::CreateFileW(path.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!image->file_) {
        return nullptr;
    }
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(image->file_.get(), &size) == 0 || size.QuadPart < 64) {
        return nullptr;
    }
    image->size_ = static_cast<std::size_t>(size.QuadPart);
    image->mapping_.reset(
        ::CreateFileMappingW(image->file_.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!image->mapping_) {
        return nullptr;
    }
    image->view_ = static_cast<const std::uint8_t*>(
        ::MapViewOfFile(image->mapping_.get(), FILE_MAP_READ, 0, 0, 0));
    if (image->view_ == nullptr || !image->parse()) {
        return nullptr;
    }
    return image;
}

PeImage::~PeImage() {
    if (view_ != nullptr) {
        ::UnmapViewOfFile(view_);
    }
}

bool PeImage::parse() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(view_);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > size_) {
        return false;
    }
    const auto* nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(view_ + dos->e_lfanew);
    if (nt32->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    machine_ = nt32->FileHeader.Machine;
    const WORD magic = nt32->OptionalHeader.Magic;
    const IMAGE_DATA_DIRECTORY* directories = nullptr;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        is_64_ = false;
        directories = nt32->OptionalHeader.DataDirectory;
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
               static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= size_) {
        is_64_ = true;
        directories = reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt32)->OptionalHeader.DataDirectory;
    } else {
        return false;
    }
    import_rva_ = directories[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    export_rva_ = directories[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    export_size_ = directories[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

    const std::size_t sections_at = static_cast<std::size_t>(dos->e_lfanew) + sizeof(DWORD) +
                                    sizeof(IMAGE_FILE_HEADER) +
                                    nt32->FileHeader.SizeOfOptionalHeader;
    const std::size_t count = nt32->FileHeader.NumberOfSections;
    if (sections_at + count * sizeof(IMAGE_SECTION_HEADER) > size_) {
        return false;
    }
    const auto* headers = reinterpret_cast<const IMAGE_SECTION_HEADER*>(view_ + sections_at);
    for (std::size_t i = 0; i < count; ++i) {
        sections_.push_back({headers[i].VirtualAddress, headers[i].SizeOfRawData, headers[i].PointerToRawData});
    }
    return true;
}

const std::uint8_t* PeImage::at_rva(std::uint32_t rva, std::size_t length) const {
    for (const Section& s : sections_) {
        if (rva >= s.rva && rva - s.rva < s.size) {
            const std::size_t offset = static_cast<std::size_t>(s.offset) + (rva - s.rva);
            if (offset + length > size_) {
                return nullptr;
            }
            return view_ + offset;
        }
    }
    return nullptr;
}

std::string PeImage::string_at(std::uint32_t rva) const {
    const std::uint8_t* p = at_rva(rva);
    if (p == nullptr) {
        return {};
    }
    const std::uint8_t* limit = view_ + size_;
    std::string out;
    while (p < limit && *p != 0 && out.size() < kMaxNameChars) {
        out.push_back(static_cast<char>(*p++));
    }
    return out;
}

std::vector<PeImage::ImportedModule> PeImage::imports() const {
    std::vector<ImportedModule> modules;
    if (import_rva_ == 0) {
        return modules;
    }
    const std::size_t thunk_size = is_64_ ? sizeof(std::uint64_t) : sizeof(std::uint32_t);
    for (std::uint32_t i = 0; i < kMaxImportModules; ++i) {
        const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            at_rva(import_rva_ + i * sizeof(IMAGE_IMPORT_DESCRIPTOR),
                   sizeof(IMAGE_IMPORT_DESCRIPTOR)));
        if (descriptor == nullptr || descriptor->Name == 0) {
            break;
        }
        ImportedModule module;
        module.name = string_at(descriptor->Name);
        const std::uint32_t thunks =
            descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk
                                                : descriptor->FirstThunk;
        for (std::uint32_t t = 0; t < kMaxImportFunctions; ++t) {
            const std::uint8_t* entry = at_rva(thunks + t * thunk_size, thunk_size);
            if (entry == nullptr) {
                break;
            }
            std::uint64_t value = 0;
            std::memcpy(&value, entry, thunk_size);
            if (value == 0) {
                break;
            }
            ImportedFunction function;
            const std::uint64_t ordinal_flag =
                is_64_ ? IMAGE_ORDINAL_FLAG64 : static_cast<std::uint64_t>(IMAGE_ORDINAL_FLAG32);
            if ((value & ordinal_flag) != 0) {
                function.by_ordinal = true;
                function.ordinal = static_cast<std::uint16_t>(value & 0xFFFF);
            } else {
                function.name = string_at(static_cast<std::uint32_t>(value) + sizeof(WORD));
            }
            module.functions.push_back(std::move(function));
        }
        if (!module.name.empty()) {
            modules.push_back(std::move(module));
        }
    }
    return modules;
}

const PeImage::ExportTable& PeImage::exports() const {
    if (exports_) {
        return *exports_;
    }
    exports_.emplace();
    ExportTable& table = *exports_;
    if (export_rva_ == 0) {
        return table;
    }
    const auto* directory = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        at_rva(export_rva_, sizeof(IMAGE_EXPORT_DIRECTORY)));
    if (directory == nullptr) {
        return table;
    }
    const auto* functions = reinterpret_cast<const std::uint32_t*>(
        at_rva(directory->AddressOfFunctions, directory->NumberOfFunctions * sizeof(std::uint32_t)));
    const auto* names = reinterpret_cast<const std::uint32_t*>(
        at_rva(directory->AddressOfNames, directory->NumberOfNames * sizeof(std::uint32_t)));
    const auto* name_ordinals = reinterpret_cast<const std::uint16_t*>(at_rva(
        directory->AddressOfNameOrdinals, directory->NumberOfNames * sizeof(std::uint16_t)));
    if (functions == nullptr) {
        return table;
    }
    const auto entry = [&](std::uint32_t index) {
        Export e;
        const std::uint32_t target = functions[index];
        if (target >= export_rva_ && target - export_rva_ < export_size_) {
            e.forwarder = string_at(target);
        }
        return e;
    };
    for (std::uint32_t i = 0; i < directory->NumberOfFunctions; ++i) {
        if (functions[i] != 0) {
            table.by_ordinal.emplace(directory->Base + i, entry(i));
        }
    }
    if (names != nullptr && name_ordinals != nullptr) {
        for (std::uint32_t i = 0; i < directory->NumberOfNames; ++i) {
            if (name_ordinals[i] < directory->NumberOfFunctions && functions[name_ordinals[i]] != 0) {
                table.by_name.emplace(string_at(names[i]), entry(name_ordinals[i]));
            }
        }
    }
    return table;
}

}
