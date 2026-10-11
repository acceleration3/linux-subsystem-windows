#pragma once

#include <array>

namespace lsw::agent::system {

// The display scales one Windows display can be given, and which of them a
// percentage becomes.
//
// Windows changes a display's scale while it runs only in fixed steps, and
// counts them from the display's recommended scale (the one it picks from
// the monitor's size and resolution): 2 is two steps above it, -1 one below.
// A display offers every step from 100 % up to ``above`` steps past its
// recommended one, which is ``below`` steps above 100 %.
class ScaleSteps {
public:
    // What DisplayConfigGetDeviceInfo's DPI scale answer gives: how many
    // steps there are below the recommended scale and above it.
    ScaleSteps(int below, int above) noexcept;

    // The step nearest ``percent`` that this display offers, counted from
    // its recommended scale. Halfway between two steps goes to the lower.
    [[nodiscard]] int relative_for(int percent) const noexcept;

    // The scale ``relative`` steps from the recommended one, in percent.
    [[nodiscard]] int percent_at(int relative) const noexcept;

private:
    static constexpr std::array<int, 12> kPercents = {100, 125, 150, 175, 200, 225,
                                                      250, 300, 350, 400, 450, 500};

    int recommended_;  // index into kPercents
    int highest_;      // index into kPercents
};

}  // namespace lsw::agent::system
