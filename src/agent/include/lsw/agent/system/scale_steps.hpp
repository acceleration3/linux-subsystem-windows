#pragma once

#include <array>

namespace lsw::agent::system {
class ScaleSteps {
public:
    ScaleSteps(int below, int above) noexcept;

    [[nodiscard]] int relative_for(int percent) const noexcept;

    [[nodiscard]] int percent_at(int relative) const noexcept;

private:
    static constexpr std::array<int, 12> kPercents = {100, 125, 150, 175, 200, 225,
                                                      250, 300, 350, 400, 450, 500};

    int recommended_;
    int highest_;
};

}
