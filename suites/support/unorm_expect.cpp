#include "support/unorm_expect.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites {

bool UnormExpect::accepts(UnormRule rule, double value, unsigned bits, std::uint32_t got) {
    const double top = static_cast<double>((1ull << bits) - 1ull);
    const double scaled = std::round(std::min(1.0, std::max(0.0, value)) * top * 1e4) / 1e4;
    const double have = static_cast<double>(got);
    if (rule == UnormRule::Direct3d) {
        return std::fabs(have - scaled) <= 0.6;
    }
    return scaled == std::floor(scaled) ? have == scaled : have == std::floor(scaled) || have == std::ceil(scaled);
}

bool UnormExpect::rgba8(UnormRule rule, const float expected[4], std::uint32_t got) {
    for (int c = 0; c < 4; ++c) {
        if (!accepts8(rule, expected[c], (got >> (8 * c)) & 0xFFu)) {
            return false;
        }
    }
    return true;
}

}  // namespace lsw::suites
