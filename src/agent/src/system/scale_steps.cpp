#include "lsw/agent/system/scale_steps.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace lsw::agent::system {

ScaleSteps::ScaleSteps(int below, int above) noexcept {
    const int last = static_cast<int>(kPercents.size()) - 1;
    recommended_ = std::clamp(below, 0, last);
    highest_ = std::clamp(recommended_ + std::max(above, 0), recommended_, last);
}

int ScaleSteps::relative_for(int percent) const noexcept {
    int nearest = 0;
    for (int index = 1; index <= highest_; ++index) {
        const int here = kPercents[static_cast<std::size_t>(index)];
        const int best = kPercents[static_cast<std::size_t>(nearest)];
        if (std::abs(here - percent) < std::abs(best - percent)) {
            nearest = index;
        }
    }
    return nearest - recommended_;
}

int ScaleSteps::percent_at(int relative) const noexcept {
    return kPercents[static_cast<std::size_t>(std::clamp(recommended_ + relative, 0, highest_))];
}

}  // namespace lsw::agent::system
