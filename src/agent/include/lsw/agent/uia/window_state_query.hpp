#pragma once

#include "lsw/agent/uia/window_state.hpp"

namespace lsw::agent::uia {

class UiaWindow;

class WindowStateQuery {
public:
    WindowStateQuery() = delete;

    [[nodiscard]] static WindowState for_window(const UiaWindow& window);

    [[nodiscard]] static const char* visual_state_name(int state);
};

}
