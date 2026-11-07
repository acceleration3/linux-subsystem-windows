#include "support/packed_codec.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites {

double PackedCodec::unpack_small(std::uint32_t raw, int mantissa_bits) {
    const std::uint32_t mantissa = raw & ((1u << mantissa_bits) - 1);
    const int exponent = static_cast<int>((raw >> mantissa_bits) & 31);
    if (exponent == 0) {
        return std::ldexp(static_cast<double>(mantissa), -14 - mantissa_bits);
    }
    if (exponent == 31) {
        return mantissa == 0 ? INFINITY : NAN;
    }
    return std::ldexp(1.0 + std::ldexp(static_cast<double>(mantissa), -mantissa_bits), exponent - 15);
}

double PackedCodec::small_max(int mantissa_bits) {
    return std::ldexp(2.0 - std::ldexp(1.0, -mantissa_bits), 15);
}

std::uint32_t PackedCodec::pack_small(double value, int mantissa_bits) {
    const std::uint32_t exponent_all = 31u << mantissa_bits;
    if (std::isnan(value)) {
        return exponent_all | (1u << (mantissa_bits - 1));
    }
    if (value <= 0.0) {
        return 0;
    }
    if (std::isinf(value)) {
        return exponent_all;
    }
    if (value >= small_max(mantissa_bits) * (1.0 + std::ldexp(1.0, -mantissa_bits - 1))) {
        return exponent_all;
    }
    int exponent = 0;
    const double fraction = std::frexp(value, &exponent);
    int biased = exponent - 1 + 15;
    double scaled = 0.0;
    if (biased <= 0) {
        scaled = std::ldexp(value, 14 + mantissa_bits);
        biased = 0;
    } else {
        scaled = std::ldexp(fraction * 2.0 - 1.0, mantissa_bits);
    }
    double rounded = std::nearbyint(scaled);
    std::uint32_t mantissa = static_cast<std::uint32_t>(rounded);
    if (mantissa >= (1u << mantissa_bits)) {
        mantissa = 0;
        ++biased;
    }
    if (biased >= 31) {
        return exponent_all;
    }
    return (static_cast<std::uint32_t>(biased) << mantissa_bits) | mantissa;
}

std::uint32_t PackedCodec::encode_r11g11b10(const double rgb[3]) {
    return pack_small(rgb[0], 6) | (pack_small(rgb[1], 6) << 11) | (pack_small(rgb[2], 5) << 22);
}

void PackedCodec::decode_r11g11b10(std::uint32_t word, double rgb[3]) {
    rgb[0] = unpack_small(word & 0x7ff, 6);
    rgb[1] = unpack_small((word >> 11) & 0x7ff, 6);
    rgb[2] = unpack_small((word >> 22) & 0x3ff, 5);
}

std::uint32_t PackedCodec::encode_rgb9e5(const double rgb[3]) {
    constexpr int kExpBias = 15;
    constexpr int kMaxExp = 31;
    constexpr int kMantissa = 9;
    const double max_value = std::ldexp(511.0 / 512.0, kMaxExp - kExpBias);
    double clamped[3];
    for (int i = 0; i < 3; ++i) {
        const double v = rgb[i];
        clamped[i] = !(v > 0.0) ? 0.0 : std::min(v, max_value);
    }
    const double max_channel = std::max({clamped[0], clamped[1], clamped[2]});
    int exp_shared = std::max(-kExpBias - 1, static_cast<int>(std::floor(std::log2(std::max(max_channel, 1e-300))))) + 1 + kExpBias;
    if (max_channel == 0.0) {
        exp_shared = 0;
    }
    double denom = std::ldexp(1.0, exp_shared - kExpBias - kMantissa);
    if (max_channel > 0.0 && static_cast<int>(std::floor(max_channel / denom + 0.5)) == (1 << kMantissa)) {
        ++exp_shared;
        denom *= 2.0;
    }
    std::uint32_t word = static_cast<std::uint32_t>(exp_shared) << 27;
    for (int i = 0; i < 3; ++i) {
        const std::uint32_t m = static_cast<std::uint32_t>(std::floor(clamped[i] / denom + 0.5));
        word |= (m & 0x1ff) << (9 * i);
    }
    return word;
}

void PackedCodec::decode_rgb9e5(std::uint32_t word, double rgb[3]) {
    const int exponent = static_cast<int>(word >> 27);
    const double scale = std::ldexp(1.0, exponent - 15 - 9);
    for (int i = 0; i < 3; ++i) {
        rgb[i] = static_cast<double>((word >> (9 * i)) & 0x1ff) * scale;
    }
}

}  // namespace lsw::suites
