#pragma once

namespace lsw::agent::win {

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

    // Mouse position, in whichever space ``client_space`` names.
    int x = 0;
    int y = 0;
    // Which origin x/y are measured from.
    //
    // false — the window's DWM extended frame bounds, which is what a
    //   captured frame shows: title bar and border included, so a click
    //   in the mirror is at the same offset in the frame.
    // true  — the client area. A window presented by the Vulkan replay
    //   shows the swapchain and nothing else, so its top-left is the
    //   client origin and using the frame origin puts every click low
    //   and right by the height of the title bar.
    bool client_space = false;
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

}  // namespace lsw::agent::win
