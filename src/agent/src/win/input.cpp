#include "lsw/agent/win/input.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

namespace lsw::agent::win {

namespace {

constexpr unsigned kModAlt = 4;

// Extended-key VKs need KEYEVENTF_EXTENDEDKEY so the system builds the
// right scan code (arrow cluster vs numpad, right-hand modifiers, …).
bool is_extended_vk(unsigned vk) {
    switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

// Bring the target window to the foreground so synthetic input (which
// is global / goes to the focused window) lands on it. Best-effort:
// even if SetForegroundWindow is refused, positioning the cursor over
// the window (done by the caller) still routes mouse input correctly.
void bring_foreground(HWND top) {
    if (::GetForegroundWindow() == top) {
        return;
    }
    if (::IsIconic(top)) {
        ::ShowWindow(top, SW_RESTORE);
    }
    ::AllowSetForegroundWindow(ASFW_ANY);
    ::SetForegroundWindow(top);
}

void send_mouse(DWORD flags, DWORD data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;  // no MOVE flag → acts at the current cursor pos
    in.mi.mouseData = data;
    ::SendInput(1, &in, sizeof(INPUT));
}

void send_button(int button, bool down, bool dbl) {
    DWORD d = 0;
    DWORD u = 0;
    switch (button) {
    case 1: d = MOUSEEVENTF_RIGHTDOWN;  u = MOUSEEVENTF_RIGHTUP;  break;
    case 2: d = MOUSEEVENTF_MIDDLEDOWN; u = MOUSEEVENTF_MIDDLEUP; break;
    default: d = MOUSEEVENTF_LEFTDOWN;  u = MOUSEEVENTF_LEFTUP;   break;
    }
    if (dbl) {
        // Two real down/up pairs in quick succession → the system
        // synthesises a genuine double-click (timing + position based).
        send_mouse(d);
        send_mouse(u);
        send_mouse(d);
        send_mouse(u);
    } else {
        send_mouse(down ? d : u);
    }
}

void send_key(unsigned vk, unsigned scan, bool up, bool /*alt*/) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(vk);
    in.ki.wScan = static_cast<WORD>(
        scan != 0 ? scan : ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = 0;
    if (is_extended_vk(vk)) {
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (up) {
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    ::SendInput(1, &in, sizeof(INPUT));
}

void send_unicode(unsigned codepoint) {
    // KEYEVENTF_UNICODE injects the exact character irrespective of
    // the guest keyboard layout / shift state — no WM_CHAR vs
    // WM_KEYDOWN ambiguity, so no double-typing.
    INPUT in[2]{};
    for (int i = 0; i < 2; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wScan = static_cast<WORD>(codepoint);
        in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
    }
    ::SendInput(2, in, sizeof(INPUT));
}

}  // namespace

bool Input::inject(std::uintptr_t top_raw, const InputEvent& ev) {
    auto* top = reinterpret_cast<HWND>(top_raw);
    if (top == nullptr || ::IsWindow(top) == FALSE) {
        return false;
    }

    switch (ev.kind) {
    case InputEvent::Kind::MouseMove:
    case InputEvent::Kind::MouseDown:
    case InputEvent::Kind::MouseUp:
    case InputEvent::Kind::MouseDblClk:
    case InputEvent::Kind::Wheel: {
        // FULL-WINDOW model: the mirror frame coords are relative to
        // the window's DWM extended frame bounds (same reference the
        // capture uses), NOT the client area — so map via that origin
        // instead of ClientToScreen, or clicks are off by the
        // title/menu-bar height. Then a REAL absolute mouse move so
        // Windows hit-tests/routes it itself (no PostMessage/NCHITTEST
        // dance; works with games / raw input / custom chrome) and
        // un-hides the cursor + drives the WinUI pointer pipeline.
        POINT p{ev.x, ev.y};
        if (ev.client_space) {
            // The sender's window shows the client area alone — a
            // Vulkan swapchain, with no title bar in it — so its
            // top-left IS the client origin.
            ::ClientToScreen(top, &p);
        } else {
            RECT fr{};
            if (FAILED(::DwmGetWindowAttribute(top, DWMWA_EXTENDED_FRAME_BOUNDS,
                                               &fr, sizeof(fr)))) {
                ::GetWindowRect(top, &fr);
            }
            p.x += fr.left;
            p.y += fr.top;
        }
        // Foreground only for actual button events. Wheel routes by
        // cursor position regardless of foreground, and stealing
        // focus on a wheel-over a combobox dropdown made Windows
        // dismiss the dropdown.
        if (ev.kind == InputEvent::Kind::MouseDown
            || ev.kind == InputEvent::Kind::MouseUp
            || ev.kind == InputEvent::Kind::MouseDblClk) {
            bring_foreground(top);
        }
        const int vw = ::GetSystemMetrics(SM_CXSCREEN);
        const int vh = ::GetSystemMetrics(SM_CYSCREEN);
        if (vw > 1 && vh > 1) {
            INPUT mv{};
            mv.type = INPUT_MOUSE;
            mv.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            mv.mi.dx = static_cast<LONG>(p.x * 65535LL / (vw - 1));
            mv.mi.dy = static_cast<LONG>(p.y * 65535LL / (vh - 1));
            ::SendInput(1, &mv, sizeof(mv));
        } else {
            ::SetCursorPos(p.x, p.y);
        }
        switch (ev.kind) {
        case InputEvent::Kind::MouseMove:
            // SetCursorPos already produced the move + hover.
            break;
        case InputEvent::Kind::MouseDown:
            send_button(ev.button, true, false);
            break;
        case InputEvent::Kind::MouseUp:
            send_button(ev.button, false, false);
            break;
        case InputEvent::Kind::MouseDblClk:
            send_button(ev.button, true, true);
            break;
        case InputEvent::Kind::Wheel:
            send_mouse(ev.hwheel ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL,
                       static_cast<DWORD>(ev.wheel));
            break;
        default:
            break;
        }
        return true;
    }
    case InputEvent::Kind::KeyDown:
    case InputEvent::Kind::KeyUp: {
        bring_foreground(top);
        const bool up = ev.kind == InputEvent::Kind::KeyUp;
        send_key(ev.vk, ev.scan, up, (ev.mods & kModAlt) != 0);
        return true;
    }
    case InputEvent::Kind::Char: {
        bring_foreground(top);
        send_unicode(ev.codepoint);
        return true;
    }
    }
    return false;
}

}  // namespace lsw::agent::win
