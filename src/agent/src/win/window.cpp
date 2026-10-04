#include "lsw/agent/win/window.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/process_info.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <vector>

namespace lsw::agent::win {

namespace {

// The window's frame rect: the DWM extended frame bounds (the visible
// window, drop shadow excluded), falling back to GetWindowRect.
//
// This one rectangle is the whole coordinate system. The WGC capture
// texture IS this rect — measured, not assumed: a restored Notepad
// captures 667x389 against a 667x389 frame, a maximized one 1280x754
// against 1280x754, a maximized Photoshop 1296x768 against 1296x768,
// while GetWindowRect disagrees with all three. So enumeration, props,
// input injection and capture all measure from here and need no
// conversion between them.
RECT frame_rect_of(HWND hwnd) {
    RECT fr{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr))) ||
        fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        ::GetWindowRect(hwnd, &fr);
    }
    return fr;
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<DiscoveredWindow>*>(lparam);
    if (!::IsWindowVisible(hwnd)) {
        return TRUE;
    }
    // DWM-cloaked windows are "visible" per IsWindowVisible but not
    // actually shown — UWP/packaged apps keep many such ghost
    // top-levels. Capturing them spawns dead black mirrors.
    int cloaked = 0;
    if (::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) == S_OK &&
        cloaked != 0) {
        return TRUE;
    }
    // NOTE: we deliberately do NOT filter by title or WS_EX_TOOLWINDOW
    // anymore. Menus (class "#32768"), combo-box drop-downs
    // ("ComboLbox") and modern WinUI flyouts are title-less and often
    // tool windows — the old filter dropped every one of them, which
    // is exactly why menus weren't captured. The AppWindowWatcher
    // scopes mirrors to the launched app's PID, so permissive
    // top-level enumeration here only ever surfaces that app's own
    // popups, not unrelated system chrome.
    wchar_t cls[64] = {};
    ::GetClassNameW(hwnd, cls, 64);
    if (::lstrcmpiW(cls, L"tooltips_class32") == 0) {
        return TRUE;  // pure tooltips: pure flicker, no value mirroring
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
    // Skip our own agent process so we don't capture ourselves
    // (mostly relevant when running --console rather than as a
    // service, but cheap to filter unconditionally).
    if (!image.empty() && image == ProcessInfo::self_path()) {
        return TRUE;
    }

    // FULL-WINDOW model: the mirror shows the whole window (title bar,
    // menu bar, borders) — not just the client area — so classic
    // menu-bar / system menus, which open in the NON-client region,
    // line up. Origin + size both come from the frame rect, so
    // enumeration, capture, input and placement all agree.
    const RECT fr = frame_rect_of(hwnd);
    if (fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        return TRUE;  // zero-area placeholder/ghost top-level
    }
    DiscoveredWindow w;
    w.hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    // GA_ROOTOWNER walks BOTH the parent and owner chains to the
    // top-level "owned root" — which is what we need to associate a
    // popup with the right tracked window. GW_OWNER alone returns the
    // direct owner, which for things like ComboLBox dropdowns is the
    // combobox CHILD control (not a top-level we mirror) → the popup
    // mirror would then have no tracked owner and fall through to
    // the main-window path (decorated frame + compositor-chosen
    // position). GA_ROOTOWNER returns the actual top-level instead.
    // For a window with no owner it returns the window itself; treat
    // that as "no owner".
    HWND root = ::GetAncestor(hwnd, GA_ROOTOWNER);
    w.owner = (root != nullptr && root != hwnd) ? reinterpret_cast<std::uintptr_t>(root) : 0;
    w.pid = static_cast<std::uint32_t>(pid);
    w.title = util::TextEncoding::to_utf8(title);
    w.exe_path = util::TextEncoding::to_utf8(image);
    w.client_w = fr.right - fr.left;  // now the WINDOW size
    w.client_h = fr.bottom - fr.top;
    w.screen_x = fr.left;  // now the WINDOW origin
    w.screen_y = fr.top;
    w.minimized = ::IsIconic(hwnd) != FALSE;
    w.maximized = ::IsZoomed(hwnd) != FALSE;
    w.has_caption = (::GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    out->push_back(std::move(w));
    return TRUE;
}

}  // namespace

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
    // Posted, not sent: SC_MINIMIZE and friends run an animation and a
    // full layout pass inside the target's message loop, and the RPC
    // thread has no reason to wait for that — nor to hang behind an
    // application that is busy.
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
    // PostMessage (not SendMessage) so we don't block on the target
    // window's message loop — the app processes WM_CLOSE on its own
    // schedule, possibly putting up a save-changes dialog.
    const bool ok = ::PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
    LSW_AGENT_INFO("window_capture: close_window hwnd={} {}", hwnd_raw,
                   ok ? "posted" : "post FAILED");
    return ok;
}

std::vector<DiscoveredWindow> Window::enumerate() {
    // This is the in-session implementation. When called from the
    // service process (session 0) it sees nothing because top-level
    // windows live on the user session's desktop. The host_endpoint
    // forwards list_windows to the user-session GuiHelper which
    // ends up calling this function; in the helper's process, we ARE
    // the user session, so EnumWindows works natively.
    std::vector<DiscoveredWindow> out;
    ::EnumWindows(&enum_proc, reinterpret_cast<LPARAM>(&out));
    return out;  // no per-call log: enumerate runs at the watcher poll
                 // rate (now ~50 Hz) — logging it floods the ring.
}

}  // namespace lsw::agent::win
