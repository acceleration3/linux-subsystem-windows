#include "lsw/agent/win/window.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/task_window.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <string_view>
#include <vector>

namespace lsw::agent::win {

namespace {
RECT frame_rect_of(HWND hwnd) {
    RECT fr{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr))) ||
        fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        ::GetWindowRect(hwnd, &fr);
    }
    return fr;
}

bool cloaked(HWND hwnd) {
    int state = 0;
    return ::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &state, sizeof(state)) == S_OK &&
           state != 0;
}

bool shown(HWND hwnd) {
    if (::IsWindowVisible(hwnd) == FALSE) {
        return false;
    }
    RECT rect{};
    if (::GetWindowRect(hwnd, &rect) != FALSE && rect.left == 0 && rect.top == 0 &&
        rect.right == 0 && rect.bottom == 0) {
        return false;
    }
    if (::IsIconic(hwnd) == FALSE) {
        return true;
    }
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    return ::GetWindowPlacement(hwnd, &placement) == FALSE ||
           ::IsRectEmpty(&placement.rcNormalPosition) == FALSE;
}

TaskWindow::Front front_of(HWND hwnd) {
    constexpr int kMostHops = 32;
    HWND walk = ::GetAncestor(hwnd, GA_ROOTOWNER);
    for (int hop = 0; hop < kMostHops && walk != nullptr; ++hop) {
        const HWND next = ::GetLastActivePopup(walk);
        if (next == nullptr || next == walk) {
            break;
        }
        walk = next;
        if (shown(walk)) {
            break;
        }
    }
    if (walk == nullptr || !shown(walk)) {
        return TaskWindow::Front::None;
    }
    for (int hop = 0; hop < kMostHops && walk != nullptr; ++hop) {
        if (walk == hwnd) {
            return TaskWindow::Front::Mine;
        }
        walk = ::GetWindow(walk, GW_OWNER);
    }
    return TaskWindow::Front::Another;
}

bool task_window_of(HWND hwnd, std::wstring_view class_name) {
    TaskWindow facts;
    facts.visible = ::IsWindowVisible(hwnd) != FALSE;
    facts.extended_style = static_cast<std::uint32_t>(::GetWindowLongW(hwnd, GWL_EXSTYLE));
    facts.class_name = class_name;
    facts.top_level = ::GetAncestor(hwnd, GA_ROOT) == hwnd;
    facts.shell_window = hwnd == ::GetShellWindow();
    if (!facts.qualifies()) {
        return false;
    }
    facts.cloaked = cloaked(hwnd);
    const HWND owner = ::GetWindow(hwnd, GW_OWNER);
    facts.owned = owner != nullptr;
    if (facts.owned && (facts.extended_style & TaskWindow::kAppWindow) == 0) {
        facts.owner_shown = shown(owner);
        if (!facts.owner_shown) {
            facts.front = front_of(hwnd);
        }
    }
    return facts.qualifies();
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<DiscoveredWindow>*>(lparam);
    if (auto found = Window::describe(reinterpret_cast<std::uintptr_t>(hwnd))) {
        out->push_back(std::move(*found));
    }
    return TRUE;
}

}  // namespace

std::optional<DiscoveredWindow> Window::describe(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (!::IsWindowVisible(hwnd)) {
        return std::nullopt;
    }
    if (cloaked(hwnd)) {
        return std::nullopt;
    }
    wchar_t cls[64] = {};
    ::GetClassNameW(hwnd, cls, 64);
    if (::lstrcmpiW(cls, L"tooltips_class32") == 0) {
        return std::nullopt;
    }

    int title_len = ::GetWindowTextLengthW(hwnd);
    std::wstring title;
    if (title_len > 0) {
        title.resize(static_cast<size_t>(title_len));
        ::GetWindowTextW(hwnd, title.data(), title_len + 1);
    }

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    const std::wstring image = ProcessInfo::image_path(pid);
    if (!image.empty() && image == ProcessInfo::self_path()) {
        return std::nullopt;
    }

    const RECT fr = frame_rect_of(hwnd);
    if (fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        return std::nullopt;
    }
    DiscoveredWindow w;
    w.hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    HWND root = ::GetAncestor(hwnd, GA_ROOTOWNER);
    w.owner = (root != nullptr && root != hwnd) ? reinterpret_cast<std::uintptr_t>(root) : 0;
    w.pid = static_cast<std::uint32_t>(pid);
    w.title = util::TextEncoding::to_utf8(title);
    w.exe_path = util::TextEncoding::to_utf8(image);
    w.client_w = fr.right - fr.left;
    w.client_h = fr.bottom - fr.top;
    w.screen_x = fr.left;
    w.screen_y = fr.top;
    w.minimized = ::IsIconic(hwnd) != FALSE;
    w.maximized = ::IsZoomed(hwnd) != FALSE;
    w.has_caption = (::GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    w.task_window = task_window_of(hwnd, cls);
    w.on_screen = w.minimized || ::MonitorFromRect(&fr, MONITOR_DEFAULTTONULL) != nullptr;
    return w;
}

bool Window::task_window(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    wchar_t cls[64] = {};
    ::GetClassNameW(hwnd, cls, 64);
    return task_window_of(hwnd, cls);
}

bool Window::system_command(std::uintptr_t hwnd_raw, const std::string& command) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return false;
    }
    WPARAM sc = 0;
    if (command == "minimize") {
        sc = SC_MINIMIZE;
    } else if (command == "maximize") {
        sc = SC_MAXIMIZE;
    } else if (command == "restore") {
        sc = SC_RESTORE;
    } else {
        return false;
    }
    const BOOL ok = ::PostMessageW(hwnd, WM_SYSCOMMAND, sc, 0);
    LSW_AGENT_INFO("window_command: {} hwnd={} {}", command, hwnd_raw,
                   ok != FALSE ? "ok" : "FAILED");
    return ok != FALSE;
}

bool Window::close_window(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        LSW_AGENT_WARN("window_capture: close_window invalid hwnd={}", hwnd_raw);
        return false;
    }
    const bool ok = ::PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
    LSW_AGENT_INFO("window_capture: close_window hwnd={} {}", hwnd_raw,
                   ok ? "posted" : "post FAILED");
    return ok;
}

std::vector<DiscoveredWindow> Window::enumerate() {
    std::vector<DiscoveredWindow> out;
    ::EnumWindows(&enum_proc, reinterpret_cast<LPARAM>(&out));
    return out;
}

}  // namespace lsw::agent::win
