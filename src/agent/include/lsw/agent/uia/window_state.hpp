#pragma once

#include <string>

namespace lsw::agent::uia {
struct WindowState {
    bool present = false;
    std::string visual_state;
    bool is_modal = false;
    bool is_topmost = false;
};

}
