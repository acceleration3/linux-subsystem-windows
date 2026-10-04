#pragma once

#include "lsw/agent/uia/window_state.hpp"

namespace lsw::agent::uia {

class UiaWindow;

// Reads a window's WindowPattern state: normal/maximized/minimized,
// modal, topmost.
class WindowStateQuery {
public:
    WindowStateQuery() = delete;

    [[nodiscard]] static WindowState for_window(const UiaWindow& window);

    // The wire name of a UIA ``WindowVisualState`` value: "normal",
    // "maximized", "minimized", or "unknown" for anything else. Shared
    // with the property-change handler, so a polled state and a pushed
    // one can never be spelled differently.
    [[nodiscard]] static const char* visual_state_name(int state);
};

}  // namespace lsw::agent::uia
