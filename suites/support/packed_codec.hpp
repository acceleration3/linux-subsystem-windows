#pragma once

#include <cstdint>

namespace lsw::suites {
class PackedCodec {
public:
    [[nodiscard]] static double unpack_small(std::uint32_t raw, int mantissa_bits);

    [[nodiscard]] static std::uint32_t pack_small(double value, int mantissa_bits);

    [[nodiscard]] static std::uint32_t encode_r11g11b10(const double rgb[3]);

    static void decode_r11g11b10(std::uint32_t word, double rgb[3]);

    [[nodiscard]] static std::uint32_t encode_rgb9e5(const double rgb[3]);

    static void decode_rgb9e5(std::uint32_t word, double rgb[3]);

    [[nodiscard]] static double small_max(int mantissa_bits);
};
}  // namespace lsw::suites
