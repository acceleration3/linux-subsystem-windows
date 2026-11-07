#include "support/format_codec.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites {

namespace {

FormatSpec make(int bytes, FormatKind kind, std::initializer_list<FormatField> fields) {
    FormatSpec spec;
    spec.known = true;
    spec.bytes = bytes;
    spec.kind = kind;
    for (const FormatField& field : fields) {
        spec.field[spec.count] = field;
        spec.has[field.channel] = true;
        ++spec.count;
    }
    return spec;
}

FormatSpec uniform(int comps, int bits, FormatKind kind) {
    FormatSpec spec;
    spec.known = true;
    spec.bytes = comps * bits / 8;
    spec.kind = kind;
    spec.count = comps;
    for (int i = 0; i < comps; ++i) {
        spec.field[i] = {i * bits, bits, i};
        spec.has[i] = true;
    }
    return spec;
}

FormatSpec bgra(int bits, bool alpha, FormatKind kind) {
    FormatSpec spec;
    spec.known = true;
    spec.bytes = bits * 4 / 8;
    spec.kind = kind;
    spec.count = alpha ? 4 : 3;
    const int channels[4] = {2, 1, 0, 3};
    for (int i = 0; i < spec.count; ++i) {
        spec.field[i] = {i * bits, bits, channels[i]};
        spec.has[channels[i]] = true;
    }
    return spec;
}

std::uint64_t read_bits(const std::uint8_t* texel, int shift, int bits) {
    std::uint64_t value = 0;
    for (int i = 0; i < bits; ++i) {
        const int bit = shift + i;
        value |= static_cast<std::uint64_t>((texel[bit / 8] >> (bit % 8)) & 1) << i;
    }
    return value;
}

double small_float(std::uint64_t raw, int bits, bool sign) {
    const int mantissa_bits = bits - 5 - (sign ? 1 : 0);
    const std::uint64_t mantissa = raw & ((1ull << mantissa_bits) - 1);
    const int exponent = static_cast<int>((raw >> mantissa_bits) & 31);
    const bool negative = sign && ((raw >> (bits - 1)) & 1) != 0;
    double value = 0.0;
    if (exponent == 0) {
        value = std::ldexp(static_cast<double>(mantissa), -14 - mantissa_bits);
    } else if (exponent == 31) {
        value = mantissa == 0 ? INFINITY : NAN;
    } else {
        value = std::ldexp(1.0 + std::ldexp(static_cast<double>(mantissa), -mantissa_bits), exponent - 15);
    }
    return negative ? -value : value;
}

}  // namespace

double FormatCodec::srgb_to_linear(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double FormatCodec::linear_to_srgb(double value) {
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

FormatSpec FormatCodec::spec(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return uniform(4, 32, FormatKind::Float);
    case DXGI_FORMAT_R32G32B32A32_UINT: return uniform(4, 32, FormatKind::Uint);
    case DXGI_FORMAT_R32G32B32A32_SINT: return uniform(4, 32, FormatKind::Sint);
    case DXGI_FORMAT_R32G32B32_FLOAT: return uniform(3, 32, FormatKind::Float);
    case DXGI_FORMAT_R32G32B32_UINT: return uniform(3, 32, FormatKind::Uint);
    case DXGI_FORMAT_R32G32B32_SINT: return uniform(3, 32, FormatKind::Sint);
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return uniform(4, 16, FormatKind::Float);
    case DXGI_FORMAT_R16G16B16A16_UNORM: return uniform(4, 16, FormatKind::Unorm);
    case DXGI_FORMAT_R16G16B16A16_UINT: return uniform(4, 16, FormatKind::Uint);
    case DXGI_FORMAT_R16G16B16A16_SNORM: return uniform(4, 16, FormatKind::Snorm);
    case DXGI_FORMAT_R16G16B16A16_SINT: return uniform(4, 16, FormatKind::Sint);
    case DXGI_FORMAT_R32G32_FLOAT: return uniform(2, 32, FormatKind::Float);
    case DXGI_FORMAT_R32G32_UINT: return uniform(2, 32, FormatKind::Uint);
    case DXGI_FORMAT_R32G32_SINT: return uniform(2, 32, FormatKind::Sint);
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return make(4, FormatKind::Unorm, {{0, 10, 0}, {10, 10, 1}, {20, 10, 2}, {30, 2, 3}});
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return make(4, FormatKind::Uint, {{0, 10, 0}, {10, 10, 1}, {20, 10, 2}, {30, 2, 3}});
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return make(4, FormatKind::Float, {{0, 11, 0}, {11, 11, 1}, {22, 10, 2}});
    case DXGI_FORMAT_R8G8B8A8_UNORM: return uniform(4, 8, FormatKind::Unorm);
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return uniform(4, 8, FormatKind::Srgb);
    case DXGI_FORMAT_R8G8B8A8_UINT: return uniform(4, 8, FormatKind::Uint);
    case DXGI_FORMAT_R8G8B8A8_SNORM: return uniform(4, 8, FormatKind::Snorm);
    case DXGI_FORMAT_R8G8B8A8_SINT: return uniform(4, 8, FormatKind::Sint);
    case DXGI_FORMAT_R16G16_FLOAT: return uniform(2, 16, FormatKind::Float);
    case DXGI_FORMAT_R16G16_UNORM: return uniform(2, 16, FormatKind::Unorm);
    case DXGI_FORMAT_R16G16_UINT: return uniform(2, 16, FormatKind::Uint);
    case DXGI_FORMAT_R16G16_SNORM: return uniform(2, 16, FormatKind::Snorm);
    case DXGI_FORMAT_R16G16_SINT: return uniform(2, 16, FormatKind::Sint);
    case DXGI_FORMAT_R32_FLOAT: return uniform(1, 32, FormatKind::Float);
    case DXGI_FORMAT_R32_UINT: return uniform(1, 32, FormatKind::Uint);
    case DXGI_FORMAT_R32_SINT: return uniform(1, 32, FormatKind::Sint);
    case DXGI_FORMAT_R8G8_UNORM: return uniform(2, 8, FormatKind::Unorm);
    case DXGI_FORMAT_R8G8_UINT: return uniform(2, 8, FormatKind::Uint);
    case DXGI_FORMAT_R8G8_SNORM: return uniform(2, 8, FormatKind::Snorm);
    case DXGI_FORMAT_R8G8_SINT: return uniform(2, 8, FormatKind::Sint);
    case DXGI_FORMAT_R16_FLOAT: return uniform(1, 16, FormatKind::Float);
    case DXGI_FORMAT_R16_UNORM: return uniform(1, 16, FormatKind::Unorm);
    case DXGI_FORMAT_R16_UINT: return uniform(1, 16, FormatKind::Uint);
    case DXGI_FORMAT_R16_SNORM: return uniform(1, 16, FormatKind::Snorm);
    case DXGI_FORMAT_R16_SINT: return uniform(1, 16, FormatKind::Sint);
    case DXGI_FORMAT_R8_UNORM: return uniform(1, 8, FormatKind::Unorm);
    case DXGI_FORMAT_R8_UINT: return uniform(1, 8, FormatKind::Uint);
    case DXGI_FORMAT_R8_SNORM: return uniform(1, 8, FormatKind::Snorm);
    case DXGI_FORMAT_R8_SINT: return uniform(1, 8, FormatKind::Sint);
    case DXGI_FORMAT_A8_UNORM: return make(1, FormatKind::Unorm, {{0, 8, 3}});
    case DXGI_FORMAT_B5G6R5_UNORM: return make(2, FormatKind::Unorm, {{0, 5, 2}, {5, 6, 1}, {11, 5, 0}});
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return make(2, FormatKind::Unorm, {{0, 5, 2}, {5, 5, 1}, {10, 5, 0}, {15, 1, 3}});
    case DXGI_FORMAT_B8G8R8A8_UNORM: return bgra(8, true, FormatKind::Unorm);
    case DXGI_FORMAT_B8G8R8X8_UNORM: return bgra(8, false, FormatKind::Unorm);
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return bgra(8, true, FormatKind::Srgb);
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return bgra(8, false, FormatKind::Srgb);
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return make(2, FormatKind::Unorm, {{0, 4, 2}, {4, 4, 1}, {8, 4, 0}, {12, 4, 3}});
    default: return {};
    }
}

void FormatCodec::decode(const FormatSpec& spec, const std::uint8_t* texel, double out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = 0.0;
    }
    for (int i = 0; i < spec.count; ++i) {
        const FormatField& field = spec.field[i];
        const std::uint64_t raw = read_bits(texel, field.shift, field.bits);
        double value = 0.0;
        switch (spec.kind) {
        case FormatKind::Unorm:
        case FormatKind::Srgb:
            value = static_cast<double>(raw) / static_cast<double>((1ull << field.bits) - 1);
            if (spec.kind == FormatKind::Srgb && field.channel != 3) {
                value = srgb_to_linear(value);
            }
            break;
        case FormatKind::Snorm: {
            std::int64_t signed_value = static_cast<std::int64_t>(raw);
            if ((raw >> (field.bits - 1)) & 1) {
                signed_value -= 1ll << field.bits;
            }
            value = std::fmax(static_cast<double>(signed_value) / static_cast<double>((1ll << (field.bits - 1)) - 1), -1.0);
            break;
        }
        case FormatKind::Uint:
            value = static_cast<double>(raw);
            break;
        case FormatKind::Sint: {
            std::int64_t signed_value = static_cast<std::int64_t>(raw);
            if ((raw >> (field.bits - 1)) & 1) {
                signed_value -= 1ll << field.bits;
            }
            value = static_cast<double>(signed_value);
            break;
        }
        case FormatKind::Float:
            if (field.bits == 32) {
                std::uint32_t word = static_cast<std::uint32_t>(raw);
                float single = 0.0f;
                std::memcpy(&single, &word, 4);
                value = single;
            } else {
                value = small_float(raw, field.bits, field.bits == 16);
            }
            break;
        }
        out[field.channel] = value;
    }
}

double FormatCodec::tolerance(const FormatSpec& spec, int field_index, double value) {
    const FormatField& field = spec.field[field_index];
    switch (spec.kind) {
    case FormatKind::Unorm:
        return 1.01 / static_cast<double>((1ull << field.bits) - 1);
    case FormatKind::Srgb:
        return field.channel == 3 ? 1.01 / 255.0 : 1.5 / 255.0 * 0.5 + std::fabs(value) * 0.02 + 0.002;
    case FormatKind::Snorm:
        return 1.01 / static_cast<double>((1ull << (field.bits - 1)) - 1);
    case FormatKind::Uint:
    case FormatKind::Sint:
        return 0.0;
    case FormatKind::Float:
        return field.bits == 32 ? 1e-6 + std::fabs(value) * 1e-6
                                : std::fabs(value) * std::ldexp(1.0, -(field.bits == 16 ? 9 : 4)) + 1e-4;
    }
    return 0.0;
}

bool FormatCodec::matches(const FormatSpec& spec, const std::uint8_t* texel, const double expected[4],
                          std::string& why) {
    double decoded[4];
    decode(spec, texel, decoded);
    for (int i = 0; i < spec.count; ++i) {
        const int channel = spec.field[i].channel;
        const double want = expected[channel];
        const double limit = tolerance(spec, i, want);
        if (!(std::fabs(decoded[channel] - want) <= limit)) {
            why = "channel " + std::to_string(channel) + " decoded " + std::to_string(decoded[channel]) + " expected " +
                  std::to_string(want) + " tolerance " + std::to_string(limit);
            return false;
        }
    }
    return true;
}

bool FormatCodec::is_typeless(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC7_TYPELESS:
        return true;
    default:
        return false;
    }
}

bool FormatCodec::is_block(DXGI_FORMAT format) {
    return (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM) ||
           (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

bool FormatCodec::is_depth(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
           format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D16_UNORM;
}

bool FormatCodec::is_video(DXGI_FORMAT format) {
    const int value = static_cast<int>(format);
    return (value >= 100 && value <= 114) || (value >= 130 && value <= 132);
}

int FormatCodec::block_bytes(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
        return 8;
    default:
        return is_block(format) ? 16 : 0;
    }
}

int FormatCodec::element_bytes(DXGI_FORMAT format) {
    if (is_block(format)) {
        return block_bytes(format);
    }
    const FormatSpec s = spec(format);
    if (s.known) {
        return s.bytes;
    }
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return 16;
    case DXGI_FORMAT_R32G32B32_TYPELESS: return 12;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return 8;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return 4;
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
        return 2;
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R1_UNORM:
        return 1;
    default:
        return 0;
    }
}

}  // namespace lsw::suites
