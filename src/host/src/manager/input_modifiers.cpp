#include "lsw/host/manager/input_modifiers.hpp"

namespace lsw::host::manager {

unsigned InputModifiers::of(Qt::KeyboardModifiers keys, Qt::MouseButtons buttons) {
    unsigned mask = 0;
    if (keys & Qt::ControlModifier) mask |= kCtrl;
    if (keys & Qt::ShiftModifier) mask |= kShift;
    if (keys & Qt::AltModifier) mask |= kAlt;
    if (buttons & Qt::LeftButton) mask |= kLeftButton;
    if (buttons & Qt::RightButton) mask |= kRightButton;
    if (buttons & Qt::MiddleButton) mask |= kMiddleButton;
    return mask;
}

int InputModifiers::button_index(Qt::MouseButton button) {
    if (button == Qt::RightButton) return 1;
    if (button == Qt::MiddleButton) return 2;
    return 0;
}

}  // namespace lsw::host::manager
