#pragma once

#include "support/format_codec.hpp"

#include <cstdint>
#include <vector>

namespace lsw::suites::sanity::fmt {
class FormatData {
public:
    [[nodiscard]] static std::vector<std::uint8_t> make(DXGI_FORMAT format, std::uint64_t seed, unsigned width, unsigned height);

    static void sanitize(const FormatSpec& spec, std::uint8_t* data, std::size_t texels);

    static void expected_sample(const FormatSpec& spec, const std::uint8_t* texel, double out[4]);

    [[nodiscard]] static FormatSpec spec_of(DXGI_FORMAT format);

    static void expected_texel(DXGI_FORMAT format, const FormatSpec& spec, const std::uint8_t* texel, double out[4]);

    [[nodiscard]] static bool sample_matches(const FormatSpec& spec, const double expected[4], const std::uint32_t words[4],
                                             std::string& why);

    [[nodiscard]] static std::string name_of(DXGI_FORMAT format);

    [[nodiscard]] static std::uint64_t read_field(const std::uint8_t* texel, int shift, int bits);

    static void write_field(std::uint8_t* texel, int shift, int bits, std::uint64_t value);
};
}  // namespace lsw::suites::sanity::fmt
