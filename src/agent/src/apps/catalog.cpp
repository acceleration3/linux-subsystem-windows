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
#include <string_view>
#include <unordered_set>
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
    std::wstring uninstall_string;
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

// The program a command line starts: quoted, or up to its ".exe" -
// uninstallers and services are as often registered unquoted, spaces
// and all.
std::wstring program_of(const std::wstring& command_line) {
    if (!command_line.empty() && command_line.front() == L'"') {
        const auto end = command_line.find(L'"', 1);
        return end == std::wstring::npos ? std::wstring{} : command_line.substr(1, end - 1);
    }
    const auto exe = util::WideString::lower(command_line).find(L".exe");
    return exe == std::wstring::npos ? command_line : command_line.substr(0, exe + 4);
}

std::wstring file_name_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Lowercase letters and digits only, so "Mozilla Firefox (x64 en-US)"
// and "firefox.exe" can be compared.
std::wstring name_key(std::wstring_view s) {
    std::wstring out;
    for (const wchar_t c : s) {
        if (iswalnum(c)) {
            out.push_back(static_cast<wchar_t>(towlower(c)));
        }
    }
    return out;
}

// What a DisplayIcon names is as often the uninstaller as the program:
// Steam registers uninstall.exe for both its icon and its uninstaller.
bool is_uninstaller(const std::wstring& exe, const RegEntry& e) {
    if (util::WideString::istarts_with(file_name_of(exe), L"unins")) {
        return true;
    }
    const auto uninstall_program = program_of(e.uninstall_string);
    return !uninstall_program.empty() &&
           util::WideString::lower(exe) == util::WideString::lower(uninstall_program);
}

// An .exe directly under ``dir`` (non-recursive) that is not the
// uninstaller: the one named like the entry when there is one - Steam's
// folder has steam.exe beside gameoverlayui.exe - else the first.
std::wstring find_program_in(const std::wstring& dir, const RegEntry& e) {
    if (dir.empty()) {
        return {};
    }
    std::wstring prefix = dir;
    if (prefix.back() != L'\\' && prefix.back() != L'/') {
        prefix += L'\\';
    }
    const std::wstring pattern = prefix + L"*.exe";
    WIN32_FIND_DATAW fd{};
    const win::UniqueFindHandle find{::FindFirstFileW(pattern.c_str(), &fd)};
    if (!find) {
        return {};
    }
    constexpr size_t kMinNameMatch = 3;
    const auto entry_key = name_key(e.display_name);
    std::wstring first;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        std::wstring path = prefix + fd.cFileName;
        if (is_uninstaller(path, e)) {
            continue;
        }
        std::wstring_view stem{fd.cFileName};
        stem.remove_suffix(4);
        const auto stem_key = name_key(stem);
        if (stem_key.size() >= kMinNameMatch && entry_key.find(stem_key) != std::wstring::npos) {
            return path;
        }
        if (first.empty()) {
            first = std::move(path);
        }
    } while (::FindNextFileW(find.get(), &fd));
    return first;
}

// The program to launch for an entry: its DisplayIcon when that is an
// .exe other than the uninstaller, else one from InstallLocation, else
// one beside the uninstaller - which is where Steam's is.
std::wstring resolve_program(const RegEntry& e, const std::wstring& icon_path) {
    if (util::WideString::iends_with(icon_path, L".exe") && !is_uninstaller(icon_path, e) &&
        win::FileSystem::exists(icon_path)) {
        return icon_path;
    }
    if (auto found = find_program_in(e.install_location, e); !found.empty()) {
        return found;
    }
    const auto beside = icon_path.empty() ? program_of(e.uninstall_string) : icon_path;
    const auto dir = util::WideString::parent_dir(beside);
    return dir ? find_program_in(*dir, e) : std::wstring{};
}

// The programs Windows runs as services, lowercase. An entry whose
// program is one - Mozilla's Maintenance Service - has nothing to open.
std::unordered_set<std::wstring> service_programs() {
    std::unordered_set<std::wstring> out;
    const auto services = win::RegKey::open(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services", KEY_READ);
    if (!services) {
        return out;
    }
    for (const std::wstring& name : services->subkey_names()) {
        const auto service = services->open_subkey(name.c_str(), KEY_READ);
        if (!service) {
            continue;
        }
        if (auto image = service->read_string(L"ImagePath")) {
            out.insert(util::WideString::lower(program_of(*image)));
        }
    }
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
        e.uninstall_string = sub->read_string(L"UninstallString").value_or(std::wstring{});
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

    const auto services = service_programs();
    size_t skipped_no_exe = 0;
    size_t skipped_service = 0;
    size_t with_icon = 0;

    for (const auto& e : entries) {
        const auto icon_path = strip_icon_suffix(e.display_icon);
        const auto exe_path = resolve_program(e, icon_path);
        if (exe_path.empty()) {
            ++skipped_no_exe;
            continue;
        }
        if (services.contains(util::WideString::lower(exe_path))) {
            ++skipped_service;
            continue;
        }

        const auto icon_src =
            icon_path.empty() || is_uninstaller(icon_path, e) ? exe_path : icon_path;
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
                   "resolvable exe, {} that are services)",
                   arr_out.size(), with_icon, skipped_no_exe, skipped_service);
    return doc;
}

}  // namespace lsw::agent::apps
