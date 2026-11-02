#include "lsw/agent/apps/start_menu_filter.hpp"

#include "lsw/agent/util/wide_string.hpp"

#include <algorithm>
#include <unordered_map>

namespace lsw::agent::apps {

namespace {

std::wstring file_name_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool under(const std::wstring& path, std::wstring dir) {
    if (dir.empty()) {
        return false;
    }
    if (dir.back() != L'\\' && dir.back() != L'/') {
        dir += L'\\';
    }
    return util::WideString::istarts_with(path, dir);
}

}

bool StartMenuFilter::is_uninstaller(const std::wstring& exe) {
    const std::wstring name = file_name_of(exe);
    return util::WideString::istarts_with(name, L"unins") ||
           util::WideString::istarts_with(name, L"uninstall");
}

std::vector<StartMenuEntry>
StartMenuFilter::applications(const std::vector<StartMenuEntry>& entries,
                              const std::wstring& windows_dir,
                              const std::function<bool(const std::wstring&)>& exists) {
    std::vector<std::wstring> order;
    std::unordered_map<std::wstring, std::vector<StartMenuEntry>> by_program;
    for (const StartMenuEntry& entry : entries) {
        if (entry.name.empty() || !util::WideString::iends_with(entry.target, L".exe") ||
            under(entry.target, windows_dir) || is_uninstaller(entry.target) ||
            !exists(entry.target)) {
            continue;
        }
        const std::wstring key = util::WideString::lower(entry.target);
        auto& group = by_program[key];
        if (group.empty()) {
            order.push_back(key);
        }
        group.push_back(entry);
    }
    std::vector<StartMenuEntry> out;
    for (const std::wstring& key : order) {
        const auto& group = by_program[key];
        const auto plain = std::find_if(group.begin(), group.end(),
                                        [](const StartMenuEntry& e) { return e.arguments.empty(); });
        if (plain != group.end()) {
            out.push_back(*plain);
            continue;
        }
        std::vector<std::wstring> seen;
        for (const StartMenuEntry& entry : group) {
            if (std::find(seen.begin(), seen.end(), entry.arguments) == seen.end()) {
                seen.push_back(entry.arguments);
                out.push_back(entry);
            }
        }
    }
    return out;
}

}
