#include "lsw/agent/apps/guest_drivers.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace lsw::agent::apps {

namespace {

enum class Arch { Unknown, X64, X86 };

// Where the wizard installs each build of every driver. One directory
// per architecture because a 32-bit process cannot load a 64-bit DLL,
// and every D3D8 / D3D9 title is 32-bit.
constexpr const char* kRoot = "C:\\ProgramData\\LSW";

// What gets linked, and under what name. ``installed`` is the file in
// the installed directory; ``linked_as`` is what the application's
// loader looks for, which is not always the same — ``rvk_icd.dll`` is
// named by a manifest, ``opengl32.dll`` by the DLL search order.
//
// The order matters for exactly one pair: ``vulkan-1.dll`` must be
// beside the exe before a translation layer beside the exe calls into
// it, and on a guest with no GPU driver there is no loader anywhere
// else on the search path.
struct Driver {
    const char* installed;
    const char* linked_as;
    const char* dir64;  // subdirectory of kRoot holding the x86-64 build
    const char* dir32;  // ... and the i686 one
};

constexpr std::array<Driver, 11> kDrivers = {{
    {"vulkan-1.dll", "vulkan-1.dll", "rvk", "rvk32"},
    {"rvk_icd.dll", "rvk_icd.dll", "rvk", "rvk32"},
    {"opengl32.dll", "opengl32.dll", "rvk", "rvk32"},
    {"rvk_opencl.dll", "rvk_opencl.dll", "rvk", "rvk32"},
    {"d3d8.dll", "d3d8.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d9.dll", "d3d9.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d10core.dll", "d3d10core.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d11.dll", "d3d11.dll", "dxvk\\x64", "dxvk\\x32"},
    {"dxgi.dll", "dxgi.dll", "dxvk\\x64", "dxvk\\x32"},
    {"d3d12.dll", "d3d12.dll", "vkd3d\\x64", "vkd3d\\x86"},
    {"d3d12core.dll", "d3d12core.dll", "vkd3d\\x64", "vkd3d\\x86"},
}};

// The Vulkan loader reads a manifest, not a DLL, and the path in it
// must be absolute: the Windows loader opens a driver with
// ``LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH)``, which rejects a
// relative path with error 87, loads nothing, and lets the first Vulkan
// call return VK_ERROR_OUT_OF_HOST_MEMORY — from which an application
// usually exits with no message at all.
constexpr const char* kIcdManifestName = "rvk_icd.json";

// Paths are wide from here down. The exe path arrives from the manager
// as UTF-8 and is converted once, at the boundary: the ANSI entry points
// read a path in the process code page, which is not UTF-8, so an
// application installed under a name with any non-ASCII character in it
// would have had its drivers placed somewhere else or nowhere.
[[nodiscard]] std::wstring parent_dir(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{L"."} : path.substr(0, pos);
}

[[nodiscard]] std::wstring join(const std::wstring& dir, const std::wstring& leaf) {
    return dir + L"\\" + leaf;
}

[[nodiscard]] std::wstring join(const std::wstring& dir, const char* leaf) {
    return join(dir, util::TextEncoding::to_wide(leaf));
}

// Read enough of the PE header to reach IMAGE_NT_HEADERS::FileHeader::Machine.
[[nodiscard]] Arch detect_arch(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return Arch::Unknown;
    }
    std::array<std::uint8_t, 4096> buf{};
    DWORD got = 0;
    ::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr);
    ::CloseHandle(h);
    if (got < 0x40 + 4) {
        return Arch::Unknown;
    }
    const std::uint32_t pe_off = std::uint32_t(buf[0x3C]) | (std::uint32_t(buf[0x3D]) << 8)
                                 | (std::uint32_t(buf[0x3E]) << 16)
                                 | (std::uint32_t(buf[0x3F]) << 24);
    if (pe_off + 6 > got || buf[pe_off] != 'P' || buf[pe_off + 1] != 'E' || buf[pe_off + 2] != 0
        || buf[pe_off + 3] != 0) {
        return Arch::Unknown;
    }
    const std::uint16_t machine =
        std::uint16_t(buf[pe_off + 4]) | (std::uint16_t(buf[pe_off + 5]) << 8);
    if (machine == 0x8664) {
        return Arch::X64;
    }
    if (machine == 0x014C) {
        return Arch::X86;
    }
    return Arch::Unknown;
}

[[nodiscard]] bool exists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// A path we placed: a symbolic link, or the hardlink/copy a link
// failure fell back to. Only the reparse point can be told apart with
// certainty, which is why links are the first choice.
[[nodiscard]] bool is_our_link(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Replace ``target`` with a link to ``source``.
//
// Nothing is removed unless it is ours: an application that ships its
// own d3d11.dll keeps it, and gets its own Direct3D rather than a
// broken one.
[[nodiscard]] bool link_one(const std::wstring& target, const std::wstring& source,
                            std::string* error_out) {
    if (exists(target)) {
        if (!is_our_link(target)) {
            LSW_AGENT_INFO("guest_drivers: " + util::TextEncoding::to_utf8(target)
                           + " is the application's own file; left alone");
            return true;
        }
        ::DeleteFileW(target.c_str());
    }
    // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE is rejected as an
    // invalid parameter on builds without Developer Mode, so the
    // privileged spelling is tried first and it is the fallback.
    if (::CreateSymbolicLinkW(target.c_str(), source.c_str(), 0) != FALSE
        || ::CreateSymbolicLinkW(target.c_str(), source.c_str(), 0x2 /* UNPRIVILEGED_CREATE */)
               != FALSE) {
        return true;
    }
    const DWORD link_err = ::GetLastError();
    if (::CreateHardLinkW(target.c_str(), source.c_str(), nullptr) != FALSE
        || ::CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE) {
        LSW_AGENT_DEBUG("guest_drivers: " + util::TextEncoding::to_utf8(target)
                        + " placed without a symlink (err="
                        + std::to_string(link_err) + ")");
        return true;
    }
    if (error_out != nullptr) {
        *error_out = "cannot place " + util::TextEncoding::to_utf8(target)
                     + " (symlink err=" + std::to_string(link_err)
                     + ", copy err=" + std::to_string(::GetLastError()) + ")";
    }
    return false;
}

// The manifest names the driver beside it, absolutely.
[[nodiscard]] bool write_icd_manifest(const std::wstring& dir, std::string* error_out) {
    // The manifest is JSON, which is UTF-8; the path in it is a guest
    // path, so it converts here and nowhere else.
    const std::string driver = util::TextEncoding::to_utf8(join(dir, "rvk_icd.dll"));
    std::string escaped;
    escaped.reserve(driver.size() * 2);
    for (const char c : driver) {
        if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    const std::wstring manifest = join(dir, kIcdManifestName);
    // Through std::filesystem::path: libstdc++ has no wide-string
    // ofstream constructor, and narrowing the path back would undo the
    // point of holding it wide.
    std::ofstream out{std::filesystem::path{manifest},
                      std::ios::trunc | std::ios::binary};
    if (!out) {
        if (error_out != nullptr) {
            *error_out = "cannot write " + util::TextEncoding::to_utf8(manifest);
        }
        return false;
    }
    out << "{\n"
        << "    \"file_format_version\": \"1.0.0\",\n"
        << "    \"ICD\": {\n"
        << "        \"library_path\": \"" << escaped << "\",\n"
        << "        \"api_version\": \"1.3.0\"\n"
        << "    }\n"
        << "}\n";
    return out.good();
}

}  // namespace

bool GuestDrivers::link_beside(const std::string& exe_path, std::vector<EnvVar>* env,
                               std::string* error_out) {
    const std::wstring exe = util::TextEncoding::to_wide(exe_path);
    const Arch arch = detect_arch(exe);
    if (arch == Arch::Unknown) {
        if (error_out != nullptr) {
            *error_out = "cannot read the PE architecture of " + exe_path;
        }
        return false;
    }
    const std::wstring dir = parent_dir(exe);
    LSW_AGENT_INFO("guest_drivers: staging " + std::string(arch == Arch::X64 ? "x64" : "x86")
                   + " drivers in " + util::TextEncoding::to_utf8(dir));

    bool all_ok = true;
    bool have_vulkan_icd = false;
    bool have_opencl_icd = false;
    for (const auto& driver : kDrivers) {
        const std::wstring source =
            join(join(util::TextEncoding::to_wide(kRoot),
                      arch == Arch::X64 ? driver.dir64 : driver.dir32),
                 driver.installed);
        if (!exists(source)) {
            continue;  // that driver is not installed in this guest
        }
        if (!link_one(join(dir, driver.linked_as), source, error_out)) {
            all_ok = false;
            continue;
        }
        const std::string linked{driver.linked_as};
        have_vulkan_icd = have_vulkan_icd || linked == "rvk_icd.dll";
        have_opencl_icd = have_opencl_icd || linked == "rvk_opencl.dll";
    }

    if (env == nullptr) {
        return all_ok;
    }
    // The render server's region is an IVSHMEM BAR — a different device
    // from the one carrying the frame ring, which is why the agent no
    // longer has to let go of anything for an application to render.
    env->push_back({"RVK_SERVER", "ivshmem"});
    if (have_vulkan_icd) {
        if (!write_icd_manifest(dir, error_out)) {
            all_ok = false;
        } else {
            const std::string manifest =
                util::TextEncoding::to_utf8(join(dir, kIcdManifestName));
            // Both spellings: VK_ICD_FILENAMES is what a loader older
            // than 1.3.207 reads, and a guest may have either.
            env->push_back({"VK_DRIVER_FILES", manifest});
            env->push_back({"VK_ICD_FILENAMES", manifest});
        }
    }
    if (have_opencl_icd) {
        // Windows ships the Khronos ICD loader as OpenCL.dll, and this
        // is the variable it reads to be told a driver directly —
        // without it the loader consults only
        // HKLM\SOFTWARE\Khronos\OpenCL\Vendors, which is machine-wide
        // and therefore the wrong scope for a per-application choice.
        env->push_back({"OCL_ICD_FILENAMES",
                        util::TextEncoding::to_utf8(join(dir, "rvk_opencl.dll"))});
    }
    return all_ok;
}

bool GuestDrivers::unlink_beside(const std::string& exe_path, std::string* error_out) {
    const std::wstring dir = parent_dir(util::TextEncoding::to_wide(exe_path));
    bool all_ok = true;
    for (const auto& driver : kDrivers) {
        const std::wstring target = join(dir, driver.linked_as);
        if (!is_our_link(target)) {
            continue;
        }
        if (::DeleteFileW(target.c_str()) == FALSE) {
            all_ok = false;
            if (error_out != nullptr) {
                *error_out = "cannot remove " + util::TextEncoding::to_utf8(target)
                             + " err=" + std::to_string(::GetLastError());
            }
        }
    }
    ::DeleteFileW(join(dir, kIcdManifestName).c_str());
    return all_ok;
}

}  // namespace lsw::agent::apps
