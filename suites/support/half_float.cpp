#include "support/half_float.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites {

float HalfFloat::to_float(std::uint16_t half) {
    const bool negative = (half & 0x8000) != 0;
    const int exponent = (half >> 10) & 31;
    const int mantissa = half & 0x3ff;
    float value = 0.0f;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        if (mantissa == 0) {
            value = INFINITY;
        } else {
            std::uint32_t bits = 0x7f800000u | (static_cast<std::uint32_t>(mantissa) << 13) | (negative ? 0x80000000u : 0u);
            std::memcpy(&value, &bits, 4);
            return value;
        }
    } else {
        value = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, exponent - 15);
    }
    return negative ? -value : value;
}

std::uint16_t HalfFloat::from_float(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
    const int exponent = static_cast<int>((bits >> 23) & 0xff);
    const std::uint32_t mantissa = bits & 0x7fffff;
    if (exponent == 255) {
        return static_cast<std::uint16_t>(sign | 0x7c00 | (mantissa != 0 ? (0x200 | (mantissa >> 13)) : 0));
    }
    int half_exponent = exponent - 127 + 15;
    if (half_exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00);
    }
    if (half_exponent <= 0) {
        if (half_exponent < -10) {
            return sign;
        }
        const std::uint32_t full = mantissa | 0x800000u;
        const int shift = 14 - half_exponent;
        std::uint32_t rounded = full >> shift;
        const std::uint32_t remainder = full & ((1u << shift) - 1);
        const std::uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (rounded & 1u) != 0)) {
            ++rounded;
        }
        return static_cast<std::uint16_t>(sign | rounded);
    }
    std::uint32_t rounded = (static_cast<std::uint32_t>(half_exponent) << 10) | (mantissa >> 13);
    const std::uint32_t remainder = mantissa & 0x1fff;
    if (remainder > 0x1000 || (remainder == 0x1000 && (rounded & 1u) != 0)) {
        ++rounded;
    }
    return static_cast<std::uint16_t>(sign | rounded);
}

}  // namespace lsw::suites
