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
    int cloaked = 0;
    if (::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) == S_OK &&
        cloaked != 0) {
        return TRUE;
    }
    wchar_t cls[64] = {};
    ::GetClassNameW(hwnd, cls, 64);
    if (::lstrcmpiW(cls, L"tooltips_class32") == 0) {
        return TRUE;
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
        return TRUE;
    }

    const RECT fr = frame_rect_of(hwnd);
    if (fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        return TRUE;
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
    out->push_back(std::move(w));
    return TRUE;
}

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

}
