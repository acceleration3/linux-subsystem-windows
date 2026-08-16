#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::win {

// One top-level guest window as ``Window::enumerate`` found it.
struct DiscoveredWindow {
    std::uintptr_t hwnd;     // raw HWND value, opaque to the host
    std::uintptr_t owner;    // GW_OWNER hwnd (0 = none) — for popup parenting
    std::uint32_t pid;       // owning process id — host scopes a watcher to this
    std::string title;       // window title (UTF-8)
    std::string exe_path;    // owning process exe path (UTF-8)
    int client_w;            // client-area width
    int client_h;            // client-area height
    int screen_x;            // client-area top-left, in virtual-screen coords
    int screen_y;            // (so the host can place popups relatively)
    // Show state, so a host mirror can follow a window the user
    // minimized or maximized inside the guest. Asked of the OS rather
    // than of the application: an app that draws its own caption
    // buttons — WinUI, Electron, Photoshop — tells nobody what it did
    // with a click on them, but ``IsIconic``/``IsZoomed`` answer for
    // every window regardless of who painted the chrome.
    bool minimized;
    bool maximized;
    // WS_CAPTION: this window has a title bar of its own, which is what
    // separates a dialog from a menu. Both are owned windows, and the
    // host treats them very differently — a menu is a transient popup
    // placed against its owner, a dialog is a window in its own right
    // that the user expects to drag, focus and find in the taskbar.
    bool has_caption;
};

}  // namespace lsw::agent::win
