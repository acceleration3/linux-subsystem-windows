#include "support/unorm_expect.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites {

bool UnormExpect::accepts(double value, unsigned bits, std::uint32_t got) {
    const double top = static_cast<double>((1ull << bits) - 1ull);
    const double scaled = std::round(std::min(1.0, std::max(0.0, value)) * top * 1e4) / 1e4;
    const double fraction = scaled - std::floor(scaled);
    const double nearest = std::floor(scaled + 0.5);
    if (fraction == 0.5) {
        return static_cast<double>(got) == scaled - 0.5 || static_cast<double>(got) == scaled + 0.5;
    }
    return static_cast<double>(got) == nearest;
}

bool UnormExpect::rgba8(const float expected[4], std::uint32_t got) {
    for (int c = 0; c < 4; ++c) {
        if (!accepts8(expected[c], (got >> (8 * c)) & 0xFFu)) {
            return false;
        }
    }
    return true;
}

}  // namespace lsw::suites
