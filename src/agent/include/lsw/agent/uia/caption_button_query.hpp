#pragma once

#include "lsw/agent/uia/caption_buttons.hpp"

#include <cstdint>

struct IUIAutomation;

namespace lsw::agent::uia {

// Locates a window's system caption buttons and reports them in
// captured-frame coordinates.
//
// Two subtleties it exists to hold: the max button is named "Maximize",
// "Restore" or "Restore Down" depending on window state, and Chromium
// chrome has no TitleBar element at all — its buttons hang off a generic
// Pane deeper in the tree, so the search has to fall back to the whole
// window subtree.
class CaptionButtonQuery {
public:
    CaptionButtonQuery() = delete;

    // ``efb_left`` / ``efb_top`` are the screen coordinates of the
    // captured frame's origin; returned rects are relative to it. A
    // zero-sized slot means the window has no such button.
    [[nodiscard]] static CaptionButtons run(IUIAutomation* root, std::uintptr_t hwnd,
                                            int efb_left, int efb_top);
};

}  // namespace lsw::agent::uia
