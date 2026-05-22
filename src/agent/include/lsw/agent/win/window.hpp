#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::win {

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
};

class Window {
public:
    // Enumerate all top-level visible windows that have a non-empty
    // title and aren't tool windows / our own agent. Per-process exe
    // path comes from QueryFullProcessImageNameW.
    [[nodiscard]] static std::vector<DiscoveredWindow> enumerate();

    // PNG-encoded snapshot of the window's client area only (no
    // title bar / borders), produced via
    // ``PrintWindow(.., PW_CLIENTONLY | PW_RENDERFULLCONTENT)``. The
    // ``RENDERFULLCONTENT`` flag (Win 8.1+) makes this work for
    // hardware-accelerated / DirectComposition windows. Returns raw
    // PNG bytes (no base64, no JSON wrapper) — the host_endpoint
    // ships them as the binary payload of a length-prefixed type=2
    // frame on the wire. Empty vector on failure (dead window, etc).
    [[nodiscard]] static std::vector<std::uint8_t> capture_client_png(std::uintptr_t hwnd);

    // Static properties of a top-level window, queried once so the
    // host mirror can open at the right size with the right buttons
    // and constraints — and NOT inadvertently resize the guest to a
    // Qt-default rectangle during initial show layout.
    struct WindowProps {
        bool valid = false;
        int x = 0, y = 0;          // EFB top-left (screen)
        int w = 0, h = 0;          // EFB size (current window size)
        bool resizable = false;     // WS_THICKFRAME
        bool minimizable = false;   // WS_MINIMIZEBOX
        bool maximizable = false;   // WS_MAXIMIZEBOX
        bool closable = false;      // WS_SYSMENU + SC_CLOSE not disabled
        int min_w = 0, min_h = 0;   // 0 = no app-imposed minimum
        int max_w = 0, max_h = 0;   // 0 = no app-imposed maximum
        int screen_w = 0, screen_h = 0;  // guest's primary screen size,
                                          // used by the host to clamp
                                          // resize_window so we never
                                          // ask the guest for a window
                                          // bigger than its display
        // Caption-button rects and the title-bar bounding rect live
        // on ``uia::TitleBarLayout`` now (sourced from UIA, not
        // ``WM_GETTITLEBARINFOEX``). The ``window_props`` RPC handler
        // merges this struct with the UIA layout into the JSON reply.
    };
    [[nodiscard]] static WindowProps query_props(std::uintptr_t hwnd);

    // DIAGNOSTIC: PrintWindow into a 32-bpp DIB and report the RAW
    // alpha histogram (before any post-processing) plus the 4 corner
    // pixels, as a JSON string. Used to trace why popup transparency
    // isn't coming through. Temporary.
    [[nodiscard]] static std::string capture_debug_json(std::uintptr_t hwnd);

    // Resize the window so its CLIENT area becomes exactly
    // ``client_w`` x ``client_h`` pixels (the host mirror shows the
    // client area, so the host drives this to keep the mirror 1:1).
    // The outer window rect is derived from the client size via
    // ``AdjustWindowRectEx`` against the window's current style; the
    // top-left position is left unchanged. Returns ``false`` if
    // ``hwnd`` isn't a valid window or the dimensions are non-positive.
    [[nodiscard]] static bool resize_client(std::uintptr_t hwnd, int client_w,
                                            int client_h);

    // Posts ``WM_CLOSE`` to the window — the standard "user clicked
    // the X" semantics. The app gets a chance to prompt for unsaved
    // changes etc. Returns ``false`` only if ``hwnd`` isn't a valid
    // window handle. Any prompt the app raises in response shows up
    // as a new HWND on the next ``list_windows`` tick, so the host
    // still gets to interact with it.
    [[nodiscard]] static bool close_window(std::uintptr_t hwnd);

    // Bring ``hwnd`` to the foreground (and restore if minimised). The
    // host calls this when its mirror window gains focus on the host
    // — without it, Alt-Tab onto a mirror leaves the guest window
    // unfocused since input-injection-driven focus only fires on
    // actual clicks / key presses.
    [[nodiscard]] static bool focus_window(std::uintptr_t hwnd);

    // Remove the guest window's foreground status by switching the
    // foreground to the shell window (Program Manager). Used when the
    // host mirror loses focus on the host so the guest stops
    // receiving keystrokes / hover. No-op if ``hwnd`` isn't currently
    // the foreground window — avoids stomping a focus change the
    // user already made via the guest itself.
    [[nodiscard]] static bool unfocus_window(std::uintptr_t hwnd);

    // Run ``WM_NCHITTEST`` against ``hwnd`` for the point ``(x, y)``
    // expressed in EFB-relative captured-frame pixels. The host
    // calls this on left-press AFTER its cached UIA button-rect
    // check misses — i.e. only to decide caption-drag vs HTCLIENT
    // for points outside the button strip (notably for modern apps
    // with tabs in the title bar, where Win11's wndproc correctly
    // returns HTCAPTION for the empty caption strip and HTCLIENT
    // for the tab strip). Returns the raw ``HT*`` code; ``-1`` on
    // an invalid hwnd.
    [[nodiscard]] static int hit_test_nc(std::uintptr_t hwnd, int x, int y);
};

}  // namespace lsw::agent::win
