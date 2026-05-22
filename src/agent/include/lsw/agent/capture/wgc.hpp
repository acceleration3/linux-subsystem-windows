#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::capture {

// Windows.Graphics.Capture backend. Unlike PrintWindow (which is
// structurally opaque — verified: alpha is 255 for 100% of pixels for
// every window, and DirectComposition-only flyout surfaces come back
// black), WGC captures the real DWM-composited output WITH a true
// alpha channel and renders DComp/XAML/flyout content correctly.
//
// One persistent capture session per HWND is kept alive across frames
// (creating a session per frame is far too slow); ``capture_png``
// pulls the latest frame, crops it to the window's CLIENT area (to
// match the rest of the mirror/resize/coordinate model) and returns a
// straight-alpha PNG.
class Wgc {
public:
    // True if WGC is usable on this OS (Win10 1903+ with the runtime
    // + a usable D3D11 device). Cached. When false the caller must
    // fall back to the PrintWindow path.
    [[nodiscard]] static bool available();

    // Latest client-area frame of ``hwnd`` as a PNG (BGRA, straight
    // alpha). Empty on failure (``ok`` distinguishes "not ready yet,
    // retry" from "broken"). Session is created lazily and reused.
    [[nodiscard]] static std::vector<std::uint8_t> capture_png(
        std::uintptr_t hwnd, bool& ok);

    // Drop a window's session (window closed / unsubscribed).
    static void release(std::uintptr_t hwnd);

    // DIAGNOSTIC: raw alpha histogram of one WGC frame as JSON.
    [[nodiscard]] static std::string debug_json(std::uintptr_t hwnd);
};

}  // namespace lsw::agent::capture
