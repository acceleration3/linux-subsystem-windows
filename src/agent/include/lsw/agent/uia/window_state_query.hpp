#pragma once

#include "lsw/agent/uia/window_state.hpp"

#include <cstdint>

struct IUIAutomation;

namespace lsw::agent::uia {

// Reads a window's WindowPattern state: normal/maximized/minimized,
// modal, topmost.
class WindowStateQuery {
public:
    WindowStateQuery() = delete;

    [[nodiscard]] static WindowState run(IUIAutomation* root, std::uintptr_t hwnd);
};

}  // namespace lsw::agent::uia
