#pragma once

#include <string>

namespace lsw::agent::uia {

// A window's state as the UIA WindowPattern reports it.
//
// ``present`` false means the window exposes no WindowPattern at all —
// distinct from "present but in an unknown visual state".
struct WindowState {
    bool present = false;
    std::string visual_state;  // "normal" | "maximized" | "minimized" | "unknown"
    bool is_modal = false;
    bool is_topmost = false;
};

}  // namespace lsw::agent::uia
