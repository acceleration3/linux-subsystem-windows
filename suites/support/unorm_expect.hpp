#pragma once

#include <cstdint>

namespace lsw::suites {
class UnormExpect {
public:
    [[nodiscard]] static bool accepts(double value, unsigned bits, std::uint32_t got);

    [[nodiscard]] static bool accepts8(double value, std::uint32_t got) { return accepts(value, 8, got); }

    [[nodiscard]] static bool rgba8(const float expected[4], std::uint32_t got);
};
}  // namespace lsw::suites
