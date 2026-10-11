#pragma once

namespace lsw::agent::win {

// Sets the scale of every display in this session while Windows runs, the
// way its Settings app does: through DisplayConfigSetDeviceInfo, in Windows'
// own steps (system::ScaleSteps), each display to the step it offers nearest
// the percentage asked for. Windows keeps the choice per display.
//
// Only in the session whose displays they are: the service in session 0
// has none, so this is called from the user session's helper.
class DisplayScale {
public:
    struct Outcome {
        int displays = 0;  // set, or already at the step asked for
        int percent = 0;   // the step the last of them is at, 0 with none
    };

    DisplayScale() = delete;

    [[nodiscard]] static Outcome apply(int percent);
};

}  // namespace lsw::agent::win
