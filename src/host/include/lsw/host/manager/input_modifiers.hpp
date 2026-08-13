#pragma once

#include <Qt>

namespace lsw::host::manager {

// Translates Qt modifier/button state into the bitmask the agent's
// input injector expects.
//
// The encoding is shared with the guest side (agent's InputEvent::mods);
// it lives in one class on each side so the two cannot drift.
class InputModifiers {
public:
    InputModifiers() = delete;

    static constexpr unsigned kCtrl = 1;
    static constexpr unsigned kShift = 2;
    static constexpr unsigned kAlt = 4;
    static constexpr unsigned kLeftButton = 8;
    static constexpr unsigned kRightButton = 16;
    static constexpr unsigned kMiddleButton = 32;

    [[nodiscard]] static unsigned of(Qt::KeyboardModifiers keys,
                                     Qt::MouseButtons buttons = Qt::NoButton);

    // 0 = left, 1 = right, 2 = middle — matching the agent's encoding.
    [[nodiscard]] static int button_index(Qt::MouseButton button);
};

}  // namespace lsw::host::manager
