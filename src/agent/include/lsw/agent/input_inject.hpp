#pragma once

#include <cstdint>

namespace lsw::agent {

// Synthesises user input into a guest window by PostMessageW-ing the
// classic WM_* messages. Chosen over SendInput because the guest is
// headless/mirrored: PostMessage delivers to a specific HWND without
// requiring it to be foreground, without moving the real cursor, and
// without stealing focus from whatever else runs in the session.
//
// Limitation: apps that read input via Raw Input / DirectInput /
// GetAsyncKeyState (notably some games) won't observe posted
// messages. Standard Win32 / common-control / UWP-on-Win32 apps work.
//
// Coordinates arriving from the host are in the TOP-LEVEL window's
// client space (the mirror shows that client area 1:1). The injector
// walks down to the deepest child window under the point and re-bases
// the coordinates into that child's client space so clicks land on
// the actual control (e.g. Notepad's edit control), and routes
// keyboard events to the focused control of the window's GUI thread.
struct InputEvent {
    enum class Kind {
        MouseMove,
        MouseDown,
        MouseUp,
        MouseDblClk,
        Wheel,
        KeyDown,
        KeyUp,
        Char,
    };

    Kind kind = Kind::MouseMove;

    // Mouse: client coords in the TOP-LEVEL window.
    int x = 0;
    int y = 0;
    int button = 0;     // 0 = left, 1 = right, 2 = middle (down/up/dblclk)
    int wheel = 0;      // wheel delta (±120 per notch); +y = away from user
    bool hwheel = false;  // true = horizontal wheel (WM_MOUSEHWHEEL)

    // Keyboard.
    unsigned vk = 0;        // Win32 virtual-key code (KeyDown/KeyUp)
    unsigned scan = 0;      // hardware scan code (0 → derived via MapVirtualKey)
    unsigned codepoint = 0; // UTF-16 code unit (Char)

    // Modifier / button state bitmask (shared host↔agent encoding):
    //   1 = Ctrl, 2 = Shift, 4 = Alt,
    //   8 = LButton, 16 = RButton, 32 = MButton
    unsigned mods = 0;
};

class InputInjector {
public:
    // ``top_hwnd`` is the subscribed top-level window. Returns false
    // only if the handle is invalid; a posted message that the target
    // ignores still counts as "delivered".
    [[nodiscard]] static bool inject(std::uintptr_t top_hwnd,
                                     const InputEvent& ev);
};

}  // namespace lsw::agent
