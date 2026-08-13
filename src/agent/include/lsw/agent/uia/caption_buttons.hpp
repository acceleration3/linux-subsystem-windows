#pragma once

namespace lsw::agent::uia {

// Caption-button rects as reported by UIA. All values are EFB-
// relative pixels (origin at the captured frame's top-left). Zero
// w/h on a slot means "this button isn't present on the window".
//
// Used by the host's mouse-press handler for a LOCAL O(1) check —
// no RPC. Caption-drag detection (everything that ISN'T a button)
// still uses ``WM_NCHITTEST`` per-press; modern apps put tabs /
// address bars / toolbars on the caption row and UIA can't
// discriminate those from the empty caption strip on its own, but
// the OS frame's hit-test (DefWindowProc's WM_NCHITTEST handler)
// correctly returns HTCAPTION only for the dead caption strip.
struct CaptionButtons {
    int min_x = 0,   min_y = 0,   min_w = 0,   min_h = 0;
    int max_x = 0,   max_y = 0,   max_w = 0,   max_h = 0;
    int close_x = 0, close_y = 0, close_w = 0, close_h = 0;
    int help_x = 0,  help_y = 0,  help_w = 0,  help_h = 0;
};

}  // namespace lsw::agent::uia
