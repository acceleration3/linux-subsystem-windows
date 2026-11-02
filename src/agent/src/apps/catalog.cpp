#include "lsw/agent/apps/catalog.hpp"

#include "lsw/agent/apps/apps_folder.hpp"
#include "lsw/agent/apps/file_associations.hpp"
#include "lsw/agent/apps/program_command.hpp"
#include "lsw/agent/apps/start_menu_filter.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/icon_extractor.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/agent/win/unique_handle.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <chrono>
#include <cwctype>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lsw::agent::apps {

namespace {
constexpr const wchar_t kUninstallKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

struct RegEntry {
    std::wstring display_name;
    std::wstring display_icon;
    std::wstring install_location;
    std::wstring publisher;
    std::wstring uninstall_string;
};

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

std::wstring file_name_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring name_key(std::wstring_view s) {
    std::wstring out;
    for (const wchar_t c : s) {
        if (iswalnum(c)) {
            out.push_back(static_cast<wchar_t>(towlower(c)));
        }
    }
    return out;
}

bool is_uninstaller(const std::wstring& exe, const RegEntry& e) {
    if (util::WideString::istarts_with(file_name_of(exe), L"unins")) {
        return true;
    }
    const auto uninstall_program = ProgramCommand::program_of(e.uninstall_string);
    return !uninstall_program.empty() &&
           util::WideString::lower(exe) == util::WideString::lower(uninstall_program);
}

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
    } while (::FindNextFileW(find.get(), &fd));
    return {};
}

std::wstring resolve_program(const RegEntry& e, const std::wstring& icon_path) {
    if (util::WideString::iends_with(icon_path, L".exe") && !is_uninstaller(icon_path, e) &&
        win::FileSystem::exists(icon_path)) {
        return icon_path;
    }
    return find_program_in(e.install_location, e);
}

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
            out.insert(util::WideString::lower(ProgramCommand::program_of(*image)));
        }
    }
    return out;
}

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

nlohmann::json extensions_json(const FileAssociations::Table& table, const std::wstring& program) {
    auto out = nlohmann::json::array();
    for (const std::wstring& extension : FileAssociations::of(table, program)) {
        out.push_back(util::TextEncoding::to_utf8(extension));
    }
    return out;
}

}

nlohmann::json Catalog::list() {
    LSW_AGENT_INFO("app_catalog: reading the Start menu and the Uninstall registry");
    const auto started = std::chrono::steady_clock::now();
    const auto associations = FileAssociations::scan();

    nlohmann::json doc = nlohmann::json::object();
    auto& arr_out = doc["apps"] = nlohmann::json::array();
    std::unordered_set<std::wstring> listed;
    std::unordered_set<std::wstring> listed_folders;
    size_t with_icon = 0;

    std::vector<StartMenuEntry> shortcuts;
    std::vector<AppsFolderItem> items = AppsFolder::items();
    for (const AppsFolderItem& item : items) {
        if (!item.packaged()) {
            shortcuts.push_back({item.name, item.target, item.arguments});
        }
    }
    const std::wstring windows_dir = win::Environment::expand(L"%windir%");
    const auto chosen = StartMenuFilter::applications(
        shortcuts, windows_dir, [](const std::wstring& path) { return win::FileSystem::exists(path); });
    constexpr int kIconPx = 64;
    for (const StartMenuEntry& entry : chosen) {
        const AppsFolderItem* item = nullptr;
        for (const AppsFolderItem& candidate : items) {
            if (!candidate.packaged() && candidate.name == entry.name && candidate.target == entry.target &&
                candidate.arguments == entry.arguments) {
                item = &candidate;
                break;
            }
        }
        const auto exe_u8 = util::TextEncoding::to_utf8(entry.target);
        nlohmann::json o = nlohmann::json::object();
        o["id"] = entry.arguments.empty() ? exe_u8 : exe_u8 + " " + util::TextEncoding::to_utf8(entry.arguments);
        o["name"] = util::TextEncoding::to_utf8(entry.name);
        o["exe_path"] = exe_u8;
        if (!entry.arguments.empty()) {
            o["args"] = util::TextEncoding::to_utf8(entry.arguments);
        }
        std::string icon = item != nullptr ? AppsFolder::icon(*item->shell.Get(), kIconPx) : std::string{};
        if (icon.empty()) {
            icon = win::IconExtractor::from_executable(entry.target);
        }
        if (!icon.empty()) {
            ++with_icon;
        }
        o["icon_png_base64"] = icon;
        o["extensions"] = extensions_json(associations, entry.target);
        const std::wstring lowered = util::WideString::lower(entry.target);
        listed.insert(lowered);
        if (const auto parent = util::WideString::parent_dir(lowered)) {
            listed_folders.insert(*parent);
        }
        arr_out.push_back(std::move(o));
    }
    const size_t from_start_menu = arr_out.size();

    std::vector<RegEntry> entries;
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, entries);
    enumerate_uninstall_view(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, entries);
    const auto services = service_programs();
    size_t skipped_service = 0;
    for (const auto& e : entries) {
        const auto icon_path = strip_icon_suffix(e.display_icon);
        const auto exe_path = resolve_program(e, icon_path);
        if (exe_path.empty()) {
            continue;
        }
        const std::wstring lowered = util::WideString::lower(exe_path);
        if (services.contains(lowered)) {
            ++skipped_service;
            continue;
        }
        const auto parent = util::WideString::parent_dir(lowered);
        if (listed.contains(lowered) || (parent && listed_folders.contains(*parent))) {
            continue;
        }
        const auto exe_u8 = util::TextEncoding::to_utf8(exe_path);
        nlohmann::json o = nlohmann::json::object();
        o["id"] = exe_u8;
        o["name"] = util::TextEncoding::to_utf8(e.display_name);
        o["exe_path"] = exe_u8;
        const auto b64 = win::IconExtractor::from_executable(exe_path);
        if (!b64.empty()) {
            ++with_icon;
        }
        o["icon_png_base64"] = b64;
        o["extensions"] = extensions_json(associations, exe_path);
        listed.insert(lowered);
        arr_out.push_back(std::move(o));
    }

    if (arr_out.empty()) {
        throw std::runtime_error("the Start menu listed no programs");
    }
    const auto took_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    LSW_AGENT_INFO("app_catalog: {} apps ({} from the Start menu, {} from the Uninstall registry, "
                   "{} with an icon, {} services skipped); {} programs with file types; took {} ms",
                   arr_out.size(), from_start_menu, arr_out.size() - from_start_menu, with_icon,
                   skipped_service, associations.size(), took_ms);
    return doc;
}

}
