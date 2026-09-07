#include "lsw/agent/apps/catalog.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include "lsw/agent/win/icon_extractor.hpp"
#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cwchar>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace lsw::agent::apps {

namespace {

// Add/Remove Programs is just two well-known registry hives — the
// same view ``Get-Package -ProviderName Programs`` ends up reading.
// Querying the registry directly avoids spawning powershell.exe (slow
// + can deadlock under LocalSystem due to PackageManagement module
// init), and runs in milliseconds.
//
// We read both 64-bit and 32-bit views so 32-bit apps installed on
// 64-bit Windows show up too. Per-user installs (HKCU) are skipped
// for now — running as LocalSystem, HKCU points at the system
// profile, not the logged-in user.
constexpr const wchar_t kUninstallKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

struct RegEntry {
    std::wstring display_name;
    std::wstring display_icon;
    std::wstring install_location;
    std::wstring publisher;
};

bool ends_with_icase(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = std::wcslen(suffix);
    if (s.size() < n) {
        return false;
    }
    return _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

bool starts_with_icase(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = std::wcslen(prefix);
    if (s.size() < n) {
        return false;
    }
    return _wcsnicmp(s.c_str(), prefix, n) == 0;
}

bool file_exists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// DisplayIcon is often ``"C:\Path\foo.exe",N`` with optional quoting
// and a trailing icon-resource index. Strip both so the result is the
// raw file path.
std::wstring strip_icon_suffix(std::wstring s) {
    if (!s.empty() && s.front() == L'"') {
        const auto end = s.find(L'"', 1);
        if (end != std::wstring::npos) {
            return s.substr(1, end - 1);
        }
    }
    const auto comma = s.rfind(L',');
    if (comma != std::wstring::npos) {
        bool numeric_suffix = comma + 1 < s.size();
        for (size_t i = comma + 1; i < s.size(); ++i) {
            if (!iswdigit(s[i]) && s[i] != L'-') {
                numeric_suffix = false;
                break;
            }
        }
        if (numeric_suffix) {
            return s.substr(0, comma);
        }
    }
    return s;
}

// First .exe directly under ``dir`` (one-level, non-recursive).
// Heuristic for installed-program folders that ship a single main exe.
std::wstring find_first_exe(const std::wstring& dir) {
    if (dir.empty()) {
        return {};
    }
    std::wstring pattern = dir;
    if (pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern += L'\\';
    }
    pattern += L"*.exe";
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::wstring out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        out = dir;
        if (out.back() != L'\\' && out.back() != L'/') {
            out += L'\\';
        }
        out += fd.cFileName;
        break;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return out;
}

// Read a REG_SZ / REG_EXPAND_SZ value as wstring. Empty on absent /
// wrong type. REG_EXPAND_SZ values are passed through
// ExpandEnvironmentStringsW so callers see the resolved path.
bool reg_read_string(HKEY key, const wchar_t* value, std::wstring& out) {
    DWORD type = 0;
    DWORD size = 0;
    LONG rc = ::RegQueryValueExW(key, value, nullptr, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        return false;
    }
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    rc = ::RegQueryValueExW(key, value, nullptr, nullptr,
                             reinterpret_cast<BYTE*>(buf.data()), &size);
    if (rc != ERROR_SUCCESS) {
        return false;
    }
    while (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    if (type == REG_EXPAND_SZ && !buf.empty()) {
        const DWORD n = ::ExpandEnvironmentStringsW(buf.c_str(), nullptr, 0);
        if (n > 0) {
            std::wstring ex(n, L'\0');
            const DWORD got = ::ExpandEnvironmentStringsW(buf.c_str(), ex.data(), n);
            if (got > 0) {
                while (!ex.empty() && ex.back() == L'\0') {
                    ex.pop_back();
                }
                out = std::move(ex);
                return true;
            }
        }
    }
    out = std::move(buf);
    return true;
}

DWORD reg_read_dword(HKEY key, const wchar_t* value, DWORD fallback) {
    DWORD data = fallback;
    DWORD size = sizeof(data);
    DWORD type = 0;
    if (::RegQueryValueExW(key, value, nullptr, &type,
                            reinterpret_cast<BYTE*>(&data), &size) != ERROR_SUCCESS
        || type != REG_DWORD) {
        return fallback;
    }
    return data;
}

// Filter out registry entries that aren't real user-launchable apps:
// system components, Windows updates (KB...), redistributables,
// "Update for ..." patches, etc.
bool looks_like_app(const RegEntry& e) {
    if (e.display_name.empty()) {
        return false;
    }
    static constexpr std::array<const wchar_t*, 7> kJunkPrefixes = {
        L"KB", L"Security Update", L"Update for", L"Hotfix for",
        L"Microsoft Visual C++", L"Microsoft .NET", L"Windows SDK",
    };
    for (const auto* p : kJunkPrefixes) {
        if (starts_with_icase(e.display_name, p)) {
            return false;
        }
    }
    return true;
}

void enumerate_uninstall_view(HKEY root, REGSAM view_flag,
                                std::vector<RegEntry>& out) {
    HKEY base = nullptr;
    if (::RegOpenKeyExW(root, kUninstallKey, 0, KEY_READ | view_flag, &base)
        != ERROR_SUCCESS) {
        return;
    }
    DWORD index = 0;
    wchar_t name[256];
    while (true) {
        DWORD nlen = static_cast<DWORD>(sizeof(name) / sizeof(wchar_t));
        const LONG rc = ::RegEnumKeyExW(base, index++, name, &nlen, nullptr,
                                          nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            continue;
        }
        HKEY sub = nullptr;
        if (::RegOpenKeyExW(base, name, 0, KEY_READ | view_flag, &sub)
            != ERROR_SUCCESS) {
            continue;
        }
        if (reg_read_dword(sub, L"SystemComponent", 0) != 0) {
            ::RegCloseKey(sub);
            continue;
        }
        RegEntry e;
        if (!reg_read_string(sub, L"DisplayName", e.display_name)) {
            ::RegCloseKey(sub);
            continue;
        }
        reg_read_string(sub, L"DisplayIcon", e.display_icon);
        reg_read_string(sub, L"InstallLocation", e.install_location);
        reg_read_string(sub, L"Publisher", e.publisher);
        ::RegCloseKey(sub);
        if (looks_like_app(e)) {
            out.push_back(std::move(e));
        }
    }
    ::RegCloseKey(base);
}

}  // namespace

std::string Catalog::list_json() {
    LSW_AGENT_INFO("app_catalog: enumerating Uninstall registry");

    std::vector<RegEntry> entries;
    // Both Wow64 views so 32-bit apps on 64-bit Windows show up too.
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, entries);
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, entries);

    LSW_AGENT_DEBUG(std::string{"app_catalog: "} + std::to_string(entries.size())
                    + " registry entries after junk filter");

    nlohmann::json doc = nlohmann::json::object();
    auto& arr_out = doc["apps"] = nlohmann::json::array();

    size_t skipped_no_exe = 0;
    size_t with_icon = 0;

    for (const auto& e : entries) {
        // Resolve a launch path: prefer DisplayIcon when it's an .exe
        // (most installers set it that way), fall back to the first
        // .exe in InstallLocation.
        const auto icon_path = strip_icon_suffix(e.display_icon);
        std::wstring exe_path;
        if (ends_with_icase(icon_path, L".exe") && file_exists(icon_path)) {
            exe_path = icon_path;
        } else if (!e.install_location.empty()) {
            exe_path = find_first_exe(e.install_location);
        }
        if (exe_path.empty()) {
            ++skipped_no_exe;
            continue;
        }

        const auto icon_src = !icon_path.empty() ? icon_path : exe_path;
        const auto exe_u8 = util::TextEncoding::to_utf8(exe_path);
        const auto name_u8 = util::TextEncoding::to_utf8(e.display_name);

        nlohmann::json o = nlohmann::json::object();
        o["id"] = exe_u8;
        o["name"] = name_u8;
        o["exe_path"] = exe_u8;
        const auto b64 = win::IconExtractor::from_executable(icon_src);
        if (!b64.empty()) {
            ++with_icon;
        }
        o["icon_png_base64"] = b64;
        arr_out.push_back(std::move(o));
    }

    LSW_AGENT_INFO(std::string{"app_catalog: "} + std::to_string(arr_out.size())
                   + " launchable apps (" + std::to_string(with_icon)
                   + " with icon, skipped " + std::to_string(skipped_no_exe)
                   + " without resolvable exe)");
    return doc.dump();
}

}  // namespace lsw::agent::apps
