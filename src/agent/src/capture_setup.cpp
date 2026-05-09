#include "lsw/agent/capture_setup.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace lsw::agent {

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

}  // namespace

bool CaptureSetup::enable_vulkan_for(const std::string& app_exe_path) {
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

    bool all_ok = true;
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

bool CaptureSetup::disable_vulkan_for(const std::string& app_exe_path) {
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
    for (const auto* name : kVkd3dDlls) {
        if (!unlink_one(dir + L"\\" + name)) {
            all_ok = false;
        }
    }
    return all_ok;
}

}  // namespace lsw::agent
