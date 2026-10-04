#include "lsw/agent/apps/catalog.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/icon_extractor.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/agent/win/unique_handle.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <cwctype>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
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
    const win::UniqueFindHandle find{::FindFirstFileW(pattern.c_str(), &fd)};
    if (!find) {
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
    } while (::FindNextFileW(find.get(), &fd));
    return out;
}

// Filter out registry entries that aren't real user-launchable apps:
// system components, Windows updates (KB...), redistributables,
// "Update for ..." patches, etc.
bool looks_like_app(const RegEntry& e) {
    if (e.display_name.empty()) {
        return false;
    }
    static constexpr std::array<const wchar_t*, 7> kJunkPrefixes = {
        L"KB",          L"Security Update",      L"Update for",
        L"Hotfix for",  L"Microsoft Visual C++", L"Microsoft .NET",
        L"Windows SDK",
    };
    for (const auto* p : kJunkPrefixes) {
        if (util::WideString::istarts_with(e.display_name, p)) {
            return false;
        }
    }
    return true;
}

void enumerate_uninstall_view(HKEY root, REGSAM view_flag, std::vector<RegEntry>& out) {
    const auto base = win::RegKey::open(root, kUninstallKey, KEY_READ | view_flag);
    if (!base) {
        return;
    }
    for (const std::wstring& name : base->subkey_names()) {
        const auto sub = base->open_subkey(name.c_str(), KEY_READ | view_flag);
        if (!sub || sub->read_dword(L"SystemComponent").value_or(0) != 0) {
            continue;
        }
        RegEntry e;
        auto display_name = sub->read_string(L"DisplayName");
        if (!display_name) {
            continue;
        }
        e.display_name = std::move(*display_name);
        e.display_icon = sub->read_string(L"DisplayIcon").value_or(std::wstring{});
        e.install_location = sub->read_string(L"InstallLocation").value_or(std::wstring{});
        e.publisher = sub->read_string(L"Publisher").value_or(std::wstring{});
        if (looks_like_app(e)) {
            out.push_back(std::move(e));
        }
    }
}

}  // namespace

nlohmann::json Catalog::list() {
    LSW_AGENT_INFO("app_catalog: enumerating Uninstall registry");

    std::vector<RegEntry> entries;
    // Both Wow64 views so 32-bit apps on 64-bit Windows show up too.
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, entries);
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, entries);

    LSW_AGENT_DEBUG("app_catalog: {} registry entries after junk filter", entries.size());

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
        if (util::WideString::iends_with(icon_path, L".exe") &&
            win::FileSystem::exists(icon_path)) {
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

    LSW_AGENT_INFO("app_catalog: {} launchable apps ({} with icon, skipped {} without "
                   "resolvable exe)",
                   arr_out.size(), with_icon, skipped_no_exe);
    return doc;
}

}  // namespace lsw::agent::apps
