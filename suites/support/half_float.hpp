#pragma once

#include <cstdint>

namespace lsw::suites {
class HalfFloat {
public:
    [[nodiscard]] static float to_float(std::uint16_t half);

    [[nodiscard]] static std::uint16_t from_float(float value);

    [[nodiscard]] static bool is_nan(std::uint16_t half) { return (half & 0x7c00) == 0x7c00 && (half & 0x03ff) != 0; }

    [[nodiscard]] static bool is_inf(std::uint16_t half) { return (half & 0x7fff) == 0x7c00; }

    [[nodiscard]] static bool is_denormal(std::uint16_t half) { return (half & 0x7c00) == 0 && (half & 0x03ff) != 0; }
};
}  // namespace lsw::suites
