#pragma once

#include <cstdint>

namespace lsw::suites {
enum class UnormRule { Direct3d, Khronos };

class UnormExpect {
public:
    [[nodiscard]] static bool accepts(UnormRule rule, double value, unsigned bits, std::uint32_t got);

    [[nodiscard]] static bool accepts8(UnormRule rule, double value, std::uint32_t got) { return accepts(rule, value, 8, got); }

    [[nodiscard]] static bool rgba8(UnormRule rule, const float expected[4], std::uint32_t got);
};
}  // namespace lsw::suites
