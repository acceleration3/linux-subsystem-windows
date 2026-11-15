#include "lsw/agent/win/task_window.hpp"

#include <algorithm>
#include <array>

namespace lsw::agent::win {

namespace {
constexpr std::array<std::wstring_view, 7> kSystemClasses{L"Progman",
                                                          L"WorkerW",
                                                          L"Shell_TrayWnd",
                                                          L"Shell_SecondaryTrayWnd",
                                                          L"Windows.UI.Core.CoreWindow",
                                                          L"IME",
                                                          L"MSCTFIME UI"};
}

bool TaskWindow::styled_as_task(std::uint32_t extended_style) noexcept {
    return (extended_style & kAppWindow) != 0 ||
           (extended_style & (kToolWindow | kNoActivate)) == 0;
}

bool TaskWindow::system_surface(std::wstring_view class_name) noexcept {
    return std::find(kSystemClasses.begin(), kSystemClasses.end(), class_name) !=
           kSystemClasses.end();
}

bool TaskWindow::qualifies() const noexcept {
    if (!visible || cloaked || !top_level) {
        return false;
    }
    if (!styled_as_task(extended_style)) {
        return false;
    }
    if (shell_window || system_surface(class_name)) {
        return false;
    }
    if (!owned || (extended_style & kAppWindow) != 0) {
        return true;
    }
    return !owner_shown && front != Front::Another;
}

}  // namespace lsw::agent::win
