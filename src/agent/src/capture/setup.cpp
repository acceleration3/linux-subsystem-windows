#include "lsw/agent/capture/setup.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace lsw::agent::capture {

namespace {

enum class Arch { Unknown, X64, X86 };

std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::wstring parent_dir(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, pos);
}

// Read enough of the PE header to read IMAGE_NT_HEADERS::FileHeader::Machine.
Arch detect_arch(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
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
    const std::uint32_t pe_off =
        std::uint32_t(buf[0x3C]) | (std::uint32_t(buf[0x3D]) << 8)
        | (std::uint32_t(buf[0x3E]) << 16) | (std::uint32_t(buf[0x3F]) << 24);
    if (pe_off + 6 > got) {
        return Arch::Unknown;
    }
    if (buf[pe_off] != 'P' || buf[pe_off + 1] != 'E' || buf[pe_off + 2] != 0
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

// DLL names DXVK / VKD3D-Proton ship — anything not in this list is
// left alone in the app folder (could be a legitimate app DLL).
constexpr const wchar_t* kDxvkDlls[] = {
    L"d3d8.dll", L"d3d9.dll", L"d3d10core.dll", L"d3d11.dll", L"dxgi.dll",
};
constexpr const wchar_t* kVkd3dDlls[] = {
    L"d3d12.dll",
    L"d3d12core.dll",
};

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

bool link_one(const std::wstring& target, const std::wstring& src) {
    // Remove any existing file at ``target`` so re-linking is idempotent.
    ::DeleteFileW(target.c_str());
    if (::CreateHardLinkW(target.c_str(), src.c_str(), nullptr) != FALSE) {
        LSW_AGENT_DEBUG(std::string{"capture_setup: hardlink "} + wide_to_utf8(target));
        return true;
    }
    const DWORD link_err = ::GetLastError();
    // Fall back to a plain copy (different volumes, ReFS quirks, etc.).
    if (::CopyFileW(src.c_str(), target.c_str(), FALSE) != FALSE) {
        LSW_AGENT_DEBUG(std::string{"capture_setup: copy (hardlink err="}
                        + std::to_string(link_err) + ") "
                        + wide_to_utf8(target));
        return true;
    }
    LSW_AGENT_ERROR(std::string{"capture_setup: link AND copy failed for "}
                    + wide_to_utf8(target) + " (link_err="
                    + std::to_string(link_err) + " copy_err="
                    + std::to_string(::GetLastError()) + ")");
    return false;
}

bool unlink_one(const std::wstring& target) {
    if (::GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;  // nothing to remove
    }
    if (::DeleteFileW(target.c_str()) != FALSE) {
        LSW_AGENT_DEBUG(std::string{"capture_setup: removed "} + wide_to_utf8(target));
        return true;
    }
    LSW_AGENT_ERROR(std::string{"capture_setup: DeleteFile failed for "}
                    + wide_to_utf8(target) + " err="
                    + std::to_string(::GetLastError()));
    return false;
}

// The Vulkan loader belongs next to the exe as well.
//
// A translation layer calls into vulkan-1.dll, and a VM with no GPU
// driver has no vulkan-1.dll anywhere on the system path — the wizard
// installs ours under ProgramData, which nothing outside that folder
// finds. Placing it beside the application puts it first in the DLL
// search order, which is the only mechanism available: there is no
// per-application way to point at a loader otherwise.
constexpr const wchar_t* kLoaderDll = L"vulkan-1.dll";

// Where the wizard puts each build of the driver + loader.
[[nodiscard]] std::wstring vulkan_dir_for(Arch arch) {
    return arch == Arch::X64 ? L"C:\\ProgramData\\LSW\\vulkan"
                             : L"C:\\ProgramData\\LSW\\vulkan32";
}

// Register the ICD for ``arch`` if it is not registered already.
//
// The loader finds drivers ONLY through this key, and a 32-bit process
// reads the redirected WOW6432Node view of it — so a guest installed
// before the 32-bit pair existed has a driver on disk that nothing will
// ever load. Writing it here rather than only in the answer file means
// an existing VM gains 32-bit support without being reinstalled.
//
// Idempotent, and quiet on success: this runs on every enable.
void ensure_icd_registered(Arch arch) {
    const std::wstring manifest = vulkan_dir_for(arch) + L"\\lsw_icd.json";
    if (::GetFileAttributesW(manifest.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LSW_AGENT_WARN(std::string{"capture_setup: no ICD manifest at "}
                       + wide_to_utf8(manifest) + " — that build of the driver is not "
                                                  "installed in this guest");
        return;
    }
    // KEY_WOW64_32KEY names the view a 32-bit process would read. The
    // agent is 64-bit, so without it both writes would land in the
    // 64-bit view and the 32-bit loader would still find nothing.
    const REGSAM view = arch == Arch::X64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
    HKEY key = nullptr;
    const LSTATUS open_rc = ::RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\Vulkan\\Drivers", 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | view, nullptr, &key, nullptr);
    if (open_rc != ERROR_SUCCESS) {
        LSW_AGENT_ERROR(std::string{"capture_setup: cannot open the Vulkan drivers key: "}
                        + std::to_string(open_rc));
        return;
    }
    const DWORD enabled = 0;  // 0 means enabled; the loader ignores the data
    const LSTATUS set_rc =
        ::RegSetValueExW(key, manifest.c_str(), 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled));
    ::RegCloseKey(key);
    if (set_rc != ERROR_SUCCESS) {
        LSW_AGENT_ERROR(std::string{"capture_setup: cannot register "}
                        + wide_to_utf8(manifest) + ": " + std::to_string(set_rc));
        return;
    }
    LSW_AGENT_INFO(std::string{"capture_setup: ICD registered ("}
                   + (arch == Arch::X64 ? "x64" : "x86") + "): " + wide_to_utf8(manifest));
}


}  // namespace

bool Setup::enable_vulkan_for(const std::string& app_exe_path) {
    LSW_AGENT_INFO(std::string{"capture_setup: enable_vulkan_for "} + app_exe_path);
    const std::wstring exe = utf8_to_utf16(app_exe_path);
    if (exe.empty()) {
        LSW_AGENT_ERROR("capture_setup: empty path after utf8 conversion");
        return false;
    }
    const Arch arch = detect_arch(exe);
    if (arch == Arch::Unknown) {
        LSW_AGENT_ERROR(std::string{"capture_setup: could not detect PE arch for "}
                        + app_exe_path);
        return false;
    }
    LSW_AGENT_INFO(std::string{"capture_setup: detected arch="}
                   + (arch == Arch::X64 ? "x64" : "x86"));
    const std::wstring dir = parent_dir(exe);
    const std::wstring dxvk_dir =
        std::wstring(L"C:\\ProgramData\\LSW\\dxvk\\") + (arch == Arch::X64 ? L"x64" : L"x32");
    const std::wstring vkd3d_dir =
        std::wstring(L"C:\\ProgramData\\LSW\\vkd3d\\") + (arch == Arch::X64 ? L"x64" : L"x86");

    ensure_icd_registered(arch);

    bool all_ok = true;
    // The loader first: a translation DLL beside the exe with no
    // vulkan-1.dll beside it either fails to load or silently picks up
    // some other loader.
    {
        const std::wstring src = vulkan_dir_for(arch) + L"\\" + kLoaderDll;
        if (::GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!link_one(dir + L"\\" + kLoaderDll, src)) {
                all_ok = false;
            }
        } else {
            LSW_AGENT_WARN(std::string{"capture_setup: no "} + wide_to_utf8(src)
                           + "; the application will have to find a loader elsewhere");
        }
    }
    for (const auto* name : kDxvkDlls) {
        const std::wstring src = dxvk_dir + L"\\" + name;
        if (::GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;  // archive didn't ship this DLL
        }
        const std::wstring target = dir + L"\\" + name;
        if (!link_one(target, src)) {
            all_ok = false;
        }
    }
    for (const auto* name : kVkd3dDlls) {
        const std::wstring src = vkd3d_dir + L"\\" + name;
        if (::GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        const std::wstring target = dir + L"\\" + name;
        if (!link_one(target, src)) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool Setup::disable_vulkan_for(const std::string& app_exe_path) {
    LSW_AGENT_INFO(std::string{"capture_setup: disable_vulkan_for "} + app_exe_path);
    const std::wstring exe = utf8_to_utf16(app_exe_path);
    if (exe.empty()) {
        LSW_AGENT_ERROR("capture_setup: empty path after utf8 conversion");
        return false;
    }
    const std::wstring dir = parent_dir(exe);

    bool all_ok = true;
    for (const auto* name : kDxvkDlls) {
        if (!unlink_one(dir + L"\\" + name)) {
            all_ok = false;
        }
    }
    if (!unlink_one(dir + L"\\" + kLoaderDll)) {
        all_ok = false;
    }
    for (const auto* name : kVkd3dDlls) {
        if (!unlink_one(dir + L"\\" + name)) {
            all_ok = false;
        }
    }
    return all_ok;
}

}  // namespace lsw::agent::capture
