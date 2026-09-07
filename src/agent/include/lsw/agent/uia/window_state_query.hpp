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
};

}  // namespace lsw::agent::uia
