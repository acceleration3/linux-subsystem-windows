#include "sanity/fmt/format_data.hpp"

#include "support/format_table.hpp"
#include "support/packed_codec.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

std::uint64_t FormatData::read_field(const std::uint8_t* texel, int shift, int bits) {
    std::uint64_t value = 0;
    for (int i = 0; i < bits; ++i) {
        const int bit = shift + i;
        value |= static_cast<std::uint64_t>((texel[bit / 8] >> (bit % 8)) & 1) << i;
    }
    return value;
}

void FormatData::write_field(std::uint8_t* texel, int shift, int bits, std::uint64_t value) {
    for (int i = 0; i < bits; ++i) {
        const int bit = shift + i;
        const auto mask = static_cast<std::uint8_t>(1u << (bit % 8));
        if (((value >> i) & 1) != 0) {
            texel[bit / 8] = static_cast<std::uint8_t>(texel[bit / 8] | mask);
        } else {
            texel[bit / 8] = static_cast<std::uint8_t>(texel[bit / 8] & ~mask);
        }
    }
}

void FormatData::sanitize(const FormatSpec& spec, std::uint8_t* data, std::size_t texels) {
    for (std::size_t t = 0; t < texels; ++t) {
        std::uint8_t* texel = data + t * static_cast<std::size_t>(spec.bytes);
        for (int i = 0; i < spec.count; ++i) {
            const FormatField& field = spec.field[i];
            const std::uint64_t raw = read_field(texel, field.shift, field.bits);
            if (spec.kind == FormatKind::Float) {
                const int mantissa_bits = field.bits == 32 ? 23 : (field.bits == 16 ? 10 : field.bits - 5);
                const int exponent_bits = field.bits == 32 ? 8 : 5;
                const std::uint64_t exponent_mask = ((1ull << exponent_bits) - 1) << mantissa_bits;
                if ((raw & exponent_mask) == exponent_mask) {
                    write_field(texel, field.shift, field.bits, raw & ~(1ull << mantissa_bits));
                }
            } else if (spec.kind == FormatKind::Snorm && raw == (1ull << (field.bits - 1))) {
                write_field(texel, field.shift, field.bits, raw + 1);
            }
        }
    }
}

std::vector<std::uint8_t> FormatData::make(DXGI_FORMAT format, std::uint64_t seed, unsigned width, unsigned height) {
    const FormatSpec spec = FormatCodec::spec(format);
    const bool block = FormatCodec::is_block(format);
    const unsigned columns = block ? (width + 3) / 4 : width;
    const unsigned rows = block ? (height + 3) / 4 : height;
    const std::size_t element = static_cast<std::size_t>(FormatCodec::element_bytes(format));
    std::vector<std::uint8_t> data(static_cast<std::size_t>(columns) * rows * element);
    Pattern::fill(seed, 0, data.data(), data.size());
    if (spec.known) {
        sanitize(spec, data.data(), static_cast<std::size_t>(columns) * rows);
    }
    return data;
}

void FormatData::expected_sample(const FormatSpec& spec, const std::uint8_t* texel, double out[4]) {
    FormatCodec::decode(spec, texel, out);
    if (!spec.has[3]) {
        out[3] = 1.0;
    }
}

FormatSpec FormatData::spec_of(DXGI_FORMAT format) {
    if (format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP) {
        FormatSpec spec;
        spec.known = true;
        spec.bytes = 4;
        spec.count = 3;
        spec.kind = FormatKind::Float;
        for (int i = 0; i < 3; ++i) {
            spec.field[i] = {9 * i, 9, i};
            spec.has[i] = true;
        }
        return spec;
    }
    return FormatCodec::spec(format);
}

void FormatData::expected_texel(DXGI_FORMAT format, const FormatSpec& spec, const std::uint8_t* texel, double out[4]) {
    if (format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP) {
        std::uint32_t word = 0;
        std::memcpy(&word, texel, 4);
        PackedCodec::decode_rgb9e5(word, out);
        out[3] = 1.0;
        return;
    }
    expected_sample(spec, texel, out);
}

bool FormatData::sample_matches(const FormatSpec& spec, const double expected[4], const std::uint32_t words[4], std::string& why) {
    for (int c = 0; c < 4; ++c) {
        const bool defined = spec.has[c] || c == 3;
        const double want = expected[c];
        double got = 0.0;
        if (spec.kind == FormatKind::Uint) {
            got = static_cast<double>(words[c]);
        } else if (spec.kind == FormatKind::Sint) {
            got = static_cast<double>(static_cast<std::int32_t>(words[c]));
        } else {
            float single = 0.0f;
            std::memcpy(&single, &words[c], 4);
            got = single;
        }
        if (!defined) {
            if (got != 0.0) {
                why = "channel " + std::to_string(c) + " is absent from the format but sampled " + std::to_string(got);
                return false;
            }
            continue;
        }
        if (std::isnan(want)) {
            if (!std::isnan(got)) {
                why = "channel " + std::to_string(c) + " sampled " + std::to_string(got) + ", expected NaN";
                return false;
            }
            continue;
        }
        if (std::isinf(want)) {
            if (got != want) {
                why = "channel " + std::to_string(c) + " sampled " + std::to_string(got) + ", expected " + (want > 0 ? "+" : "-") + "infinity";
                return false;
            }
            continue;
        }
        double limit = 0.0;
        switch (spec.kind) {
        case FormatKind::Unorm:
        case FormatKind::Snorm:
            limit = 2e-5;
            break;
        case FormatKind::Srgb:
            if (c == 3) {
                limit = 2e-5;
            } else {
                const double encoded = FormatCodec::linear_to_srgb(std::min(std::max(want, 0.0), 1.0));
                const double up = FormatCodec::srgb_to_linear(std::min(encoded + 1.0 / 255.0, 1.0)) - want;
                const double down = want - FormatCodec::srgb_to_linear(std::max(encoded - 1.0 / 255.0, 0.0));
                limit = std::max(up, down) + 2e-5;
            }
            break;
        case FormatKind::Float: {
            const bool wide = spec.bytes >= 4 && spec.field[0].bits == 32;
            const double tiny = wide ? 1.2e-38 : 6.2e-5;
            limit = std::fabs(want) * 1e-6;
            if (std::fabs(want) < tiny) {
                limit = tiny;
            }
            break;
        }
        default:
            break;
        }
        if (!(std::fabs(got - want) <= limit)) {
            why = "channel " + std::to_string(c) + " sampled " + std::to_string(got) + ", expected " + std::to_string(want) +
                  " within " + std::to_string(limit);
            return false;
        }
    }
    return true;
}

std::string FormatData::name_of(DXGI_FORMAT format) {
    const FormatEntry* entry = FormatTable::find(format);
    return entry != nullptr ? entry->name : "format " + std::to_string(static_cast<int>(format));
}

}  // namespace lsw::suites::sanity::fmt
