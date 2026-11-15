#pragma once

#include <dxgiformat.h>

#include <cstdint>
#include <string>

namespace lsw::suites {
enum class FormatKind { Unorm, Snorm, Uint, Sint, Float, Srgb };

struct FormatField {
    int shift = 0;
    int bits = 0;
    int channel = 0;
};

struct FormatSpec {
    bool known = false;
    int bytes = 0;
    int count = 0;
    FormatKind kind = FormatKind::Unorm;
    FormatField field[4];
    bool has[4] = {false, false, false, false};
};

class FormatCodec {
public:
    [[nodiscard]] static FormatSpec spec(DXGI_FORMAT format);

    static void decode(const FormatSpec& spec, const std::uint8_t* texel, double out[4]);

    static void encode(const FormatSpec& spec, const double values[4], std::uint8_t* texel);

    [[nodiscard]] static double tolerance(const FormatSpec& spec, int field_index, double value);

    [[nodiscard]] static bool matches(const FormatSpec& spec, const std::uint8_t* texel, const double expected[4],
                                      std::string& why);

    [[nodiscard]] static double srgb_to_linear(double value);

    [[nodiscard]] static double linear_to_srgb(double value);

    [[nodiscard]] static bool is_typeless(DXGI_FORMAT format);

    [[nodiscard]] static bool is_block(DXGI_FORMAT format);

    [[nodiscard]] static bool is_depth(DXGI_FORMAT format);

    [[nodiscard]] static bool is_video(DXGI_FORMAT format);

    [[nodiscard]] static int block_bytes(DXGI_FORMAT format);

    [[nodiscard]] static int element_bytes(DXGI_FORMAT format);
};
}  // namespace lsw::suites
