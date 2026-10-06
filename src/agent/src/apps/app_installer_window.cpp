#include "lsw/agent/apps/app_installer_window.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace lsw::agent::apps {

AppInstallerWindow::AppInstallerWindow(std::vector<win::DiscoveredWindow> before)
    : before_(std::move(before)) {}

bool AppInstallerWindow::is_installer(const win::DiscoveredWindow& window) {
    if (window.pid == 0 || window.hwnd == 0 || window.title.empty())
        return false;
    const std::string_view path(window.exe_path);
    const auto slash = path.find_last_of("/\\");
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    constexpr std::string_view expected = "appinstaller.exe";
    if (name.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != expected[i])
            return false;
    }
    return true;
}

std::optional<std::uint32_t>
AppInstallerWindow::select(const std::vector<win::DiscoveredWindow>& windows,
                           std::uintptr_t foreground, bool allow_existing) const {
    std::optional<std::uint32_t> changed;
    std::optional<std::uint32_t> existing;
    std::optional<std::uint32_t> existing_foreground;
    bool changed_ambiguous = false;
    bool existing_ambiguous = false;
    for (const auto& window : windows) {
        if (!is_installer(window))
            continue;
        if (existing && *existing != window.pid)
            existing_ambiguous = true;
        existing = window.pid;
        const bool unchanged = std::any_of(before_.begin(), before_.end(), [&](const auto& old) {
            return old.hwnd == window.hwnd && old.pid == window.pid && old.title == window.title;
        });
        if (foreground != 0 && (window.hwnd == foreground || window.owner == foreground)) {
            if (!unchanged)
                return window.pid;
            existing_foreground = window.pid;
        }
        if (!unchanged) {
            if (changed && *changed != window.pid)
                changed_ambiguous = true;
            changed = window.pid;
        }
    }
    if (changed)
        return changed_ambiguous ? std::nullopt : changed;
    // Shell activation can reuse an unchanged main window. Give it time to
    // activate first, and never guess between unrelated existing instances.
    if (!allow_existing)
        return std::nullopt;
    if (existing_foreground)
        return existing_foreground;
    return !existing_ambiguous ? existing : std::nullopt;
}

}  // namespace lsw::agent::apps
