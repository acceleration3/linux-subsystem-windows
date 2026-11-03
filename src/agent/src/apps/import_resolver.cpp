#include "lsw/agent/apps/import_resolver.hpp"

#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/reg_key.hpp"

#include <windows.h>
#include <winternl.h>

namespace lsw::agent::apps {

namespace {
constexpr wchar_t kKnownDllsKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs";
constexpr wchar_t kKnownDlls32Key[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs32";
constexpr wchar_t kApiQueryModule[] = L"api-ms-win-core-apiquery-l1-1-0.dll";
constexpr wchar_t kDllExtension[] = L".dll";
constexpr int kMaxForwarderDepth = 8;

bool api_set_present(const std::wstring& module) {
    using QueryPresence = BOOL(WINAPI*)(const UNICODE_STRING*, BOOLEAN*);
    static const QueryPresence query = [] {
        const HMODULE lib = ::LoadLibraryW(kApiQueryModule);
        return lib == nullptr ? nullptr
                              : reinterpret_cast<QueryPresence>(
                                    ::GetProcAddress(lib, "ApiSetQueryApiSetPresence"));
    }();
    if (query == nullptr) {
        return false;
    }
    std::wstring name = module;
    if (util::WideString::iends_with(name, kDllExtension)) {
        name.resize(name.size() - 4);
    }
    UNICODE_STRING ns{};
    ns.Buffer = name.data();
    ns.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    ns.MaximumLength = ns.Length;
    BOOLEAN present = FALSE;
    query(&ns, &present);
    return present != FALSE;
}

std::wstring directory_with_slash(std::wstring dir) {
    if (!dir.empty() && dir.back() != L'\\') {
        dir += L'\\';
    }
    return dir;
}

std::wstring windows_directory() {
    std::wstring out(MAX_PATH, L'\0');
    out.resize(::GetWindowsDirectoryW(out.data(), static_cast<UINT>(out.size())));
    return out;
}

std::wstring system_directory(bool wow64) {
    std::wstring out(MAX_PATH, L'\0');
    UINT n = wow64 ? ::GetSystemWow64DirectoryW(out.data(), static_cast<UINT>(out.size())) : 0;
    if (n == 0) {
        n = ::GetSystemDirectoryW(out.data(), static_cast<UINT>(out.size()));
    }
    out.resize(n);
    return out;
}

std::wstring module_name(const std::string& imported) {
    std::wstring name = util::WideString::lower(util::TextEncoding::to_wide(imported));
    if (name.find(L'.') == std::wstring::npos) {
        name += kDllExtension;
    }
    return name;
}

std::wstring display_name(const std::string& imported) {
    std::wstring name = util::TextEncoding::to_wide(imported);
    if (name.find(L'.') == std::wstring::npos) {
        name += kDllExtension;
    }
    return name;
}

std::wstring file_name_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}

ImportResolver::ImportResolver(std::wstring working_dir, std::wstring path_env)
    : working_dir_(std::move(working_dir)), path_env_(std::move(path_env)) {}

int ImportResolver::bits_of(std::uint16_t machine) {
    return machine == IMAGE_FILE_MACHINE_I386 ? 32 : 64;
}

void ImportResolver::load_known_dlls() {
    known_.clear();
    const bool wow = bits_of(machine_) == 32;
    auto key = wow ? win::RegKey::open(HKEY_LOCAL_MACHINE, kKnownDlls32Key,
                                       KEY_READ | KEY_WOW64_64KEY)
                   : std::nullopt;
    if (!key) {
        if (wow) {
            return;
        }
        key = win::RegKey::open(HKEY_LOCAL_MACHINE, kKnownDllsKey, KEY_READ | KEY_WOW64_64KEY);
    }
    if (!key) {
        return;
    }
    for (const std::wstring& name : key->value_names()) {
        const auto data = key->read_string(name.c_str());
        if (data && util::WideString::iends_with(*data, kDllExtension)) {
            known_.insert(util::WideString::lower(*data));
        }
    }
}

const win::PeImage* ImportResolver::image_at(const std::wstring& path) {
    const std::wstring key = util::WideString::lower(path);
    const auto it = images_.find(key);
    if (it != images_.end()) {
        return it->second.get();
    }
    return images_.emplace(key, win::PeImage::open(path)).first->second.get();
}

ImportResolver::Resolution ImportResolver::search(const std::wstring& module) const {
    std::vector<std::wstring> directories{app_dir_, system_dir_, windows_dir_, working_dir_};
    for (std::size_t at = 0; at <= path_env_.size();) {
        const auto end = path_env_.find(L';', at);
        const std::wstring part =
            path_env_.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
        if (!part.empty()) {
            directories.push_back(part);
        }
        if (end == std::wstring::npos) {
            break;
        }
        at = end + 1;
    }
    for (const std::wstring& dir : directories) {
        if (dir.empty()) {
            continue;
        }
        const std::wstring candidate = directory_with_slash(dir) + module;
        if (win::FileSystem::exists(candidate)) {
            return {Resolution::Source::Search, candidate};
        }
    }
    return {};
}

ImportResolver::Resolution ImportResolver::resolve(const std::wstring& module) {
    const auto cached = resolutions_.find(module);
    if (cached != resolutions_.end()) {
        return cached->second;
    }
    Resolution result;
    if (module.starts_with(L"api-") || module.starts_with(L"ext-")) {
        if (api_set_present(module)) {
            result.source = Resolution::Source::ApiSet;
        }
    } else if (known_.contains(module) &&
               win::FileSystem::exists(directory_with_slash(system_dir_) + module)) {
        result = {Resolution::Source::Known, directory_with_slash(system_dir_) + module};
    } else {
        result = search(module);
    }
    resolutions_.emplace(module, result);
    return result;
}

void ImportResolver::add(Problem problem) {
    const std::wstring key = std::to_wstring(static_cast<int>(problem.kind)) + L"|" +
                             util::WideString::lower(problem.module) + L"|" +
                             util::WideString::lower(problem.importer) + L"|" + problem.function;
    if (reported_.insert(key).second) {
        problems_.push_back(std::move(problem));
    }
}

bool ImportResolver::has_export(const std::wstring& module_path, const std::string& name,
                                std::uint16_t ordinal, bool by_ordinal, int depth) {
    const win::PeImage* image = image_at(module_path);
    if (image == nullptr) {
        return true;
    }
    const auto& table = image->exports();
    const win::PeImage::Export* found = nullptr;
    if (by_ordinal) {
        const auto it = table.by_ordinal.find(ordinal);
        found = it == table.by_ordinal.end() ? nullptr : &it->second;
    } else {
        const auto it = table.by_name.find(name);
        found = it == table.by_name.end() ? nullptr : &it->second;
    }
    if (found == nullptr) {
        return false;
    }
    if (found->forwarder.empty() || depth >= kMaxForwarderDepth) {
        return true;
    }
    const auto dot = found->forwarder.find('.');
    if (dot == std::string::npos) {
        return true;
    }
    const std::wstring target = module_name(found->forwarder.substr(0, dot));
    const Resolution resolved = resolve(target);
    if (resolved.source != Resolution::Source::Known &&
        resolved.source != Resolution::Source::Search) {
        return true;
    }
    const std::string symbol = found->forwarder.substr(dot + 1);
    if (!symbol.empty() && symbol[0] == '#') {
        return has_export(resolved.path, {}, static_cast<std::uint16_t>(std::stoul(symbol.substr(1))),
                          true, depth + 1);
    }
    return has_export(resolved.path, symbol, 0, false, depth + 1);
}

void ImportResolver::scan(const std::wstring& importer_path) {
    const win::PeImage* importer = image_at(importer_path);
    if (importer == nullptr) {
        return;
    }
    const std::wstring importer_name = file_name_of(importer_path);
    for (const auto& imported : importer->imports()) {
        const std::wstring name = module_name(imported.name);
        const std::wstring shown = display_name(imported.name);
        const Resolution resolved = resolve(name);
        if (resolved.source == Resolution::Source::None) {
            Problem p;
            p.kind = Problem::Kind::Missing;
            p.module = shown;
            p.importer = importer_name;
            add(std::move(p));
            continue;
        }
        if (resolved.source == Resolution::Source::ApiSet) {
            continue;
        }
        const win::PeImage* module = image_at(resolved.path);
        Problem p;
        p.module = shown;
        p.importer = importer_name;
        p.path = resolved.path;
        p.image_bits = bits_of(machine_);
        if (module == nullptr) {
            p.kind = Problem::Kind::Invalid;
            add(std::move(p));
            continue;
        }
        if (module->machine() != machine_) {
            p.kind = Problem::Kind::WrongBitness;
            p.module_bits = bits_of(module->machine());
            add(std::move(p));
            continue;
        }
        for (const auto& function : imported.functions) {
            if (!has_export(resolved.path, function.name, function.ordinal, function.by_ordinal, 0)) {
                Problem missing = p;
                missing.kind = Problem::Kind::MissingExport;
                missing.function = function.by_ordinal
                                       ? L"#" + std::to_wstring(function.ordinal)
                                       : util::TextEncoding::to_wide(function.name);
                add(std::move(missing));
            }
        }
        if (scanned_.insert(util::WideString::lower(resolved.path)).second) {
            scan(resolved.path);
        }
    }
}

std::vector<ImportResolver::Problem> ImportResolver::check(const std::wstring& image) {
    resolutions_.clear();
    images_.clear();
    scanned_.clear();
    reported_.clear();
    problems_.clear();

    const win::PeImage* root = image_at(image);
    if (root == nullptr) {
        return {};
    }
    machine_ = root->machine();
    app_dir_ = util::WideString::parent_dir(image).value_or(std::wstring{});
    system_dir_ = system_directory(bits_of(machine_) == 32);
    windows_dir_ = windows_directory();
    load_known_dlls();
    scanned_.insert(util::WideString::lower(image));
    scan(image);
    return std::move(problems_);
}

}
