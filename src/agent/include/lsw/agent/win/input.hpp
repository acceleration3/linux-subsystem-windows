#pragma once

#include "lsw/agent/win/input_event.hpp"

#include <cstdint>

namespace lsw::agent::win {

// Posts one InputEvent at a guest window. See input_event.hpp for why
// PostMessageW rather than SendInput, and for the coordinate space.
class Input {
public:
    // ``top_hwnd`` is the subscribed top-level window. Returns false
    // only if the handle is invalid; a posted message that the target
    // ignores still counts as "delivered".
    [[nodiscard]] static bool inject(std::uintptr_t top_hwnd,
                                     const InputEvent& ev);
};

}  // namespace lsw::agent::win
