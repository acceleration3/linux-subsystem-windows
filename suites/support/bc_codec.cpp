#include "support/bc_codec.hpp"

#include "support/pattern.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites {

namespace {

void expand565(std::uint16_t value, float out[3]) {
    const int r = (value >> 11) & 31;
    const int g = (value >> 5) & 63;
    const int b = value & 31;
    out[0] = static_cast<float>((r << 3) | (r >> 2)) / 255.0f;
    out[1] = static_cast<float>((g << 2) | (g >> 4)) / 255.0f;
    out[2] = static_cast<float>((b << 3) | (b >> 2)) / 255.0f;
}

void color_block(const std::uint8_t* block, bool force_four, float texels[16][4], bool alpha_from_punch) {
    std::uint16_t c0 = 0;
    std::uint16_t c1 = 0;
    std::memcpy(&c0, block, 2);
    std::memcpy(&c1, block + 2, 2);
    std::uint32_t indices = 0;
    std::memcpy(&indices, block + 4, 4);
    float palette[4][4];
    expand565(c0, palette[0]);
    expand565(c1, palette[1]);
    palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 1.0f;
    if (c0 > c1 || force_four) {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = (2.0f * palette[0][i] + palette[1][i]) / 3.0f;
            palette[3][i] = (palette[0][i] + 2.0f * palette[1][i]) / 3.0f;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = (palette[0][i] + palette[1][i]) / 2.0f;
            palette[3][i] = 0.0f;
        }
        if (alpha_from_punch) {
            palette[3][3] = 0.0f;
        }
    }
    for (int t = 0; t < 16; ++t) {
        const int index = static_cast<int>((indices >> (2 * t)) & 3);
        for (int i = 0; i < 4; ++i) {
            texels[t][i] = palette[index][i];
        }
    }
}

void single_channel_block(const std::uint8_t* block, bool is_signed, float out[16]) {
    float palette[8];
    if (!is_signed) {
        const float r0 = static_cast<float>(block[0]) / 255.0f;
        const float r1 = static_cast<float>(block[1]) / 255.0f;
        palette[0] = r0;
        palette[1] = r1;
        if (block[0] > block[1]) {
            for (int i = 1; i < 7; ++i) {
                palette[1 + i] = (static_cast<float>(7 - i) * r0 + static_cast<float>(i) * r1) / 7.0f;
            }
        } else {
            for (int i = 1; i < 5; ++i) {
                palette[1 + i] = (static_cast<float>(5 - i) * r0 + static_cast<float>(i) * r1) / 5.0f;
            }
            palette[6] = 0.0f;
            palette[7] = 1.0f;
        }
    } else {
        const auto snorm = [](std::int8_t v) { return std::max(static_cast<float>(v) / 127.0f, -1.0f); };
        const float r0 = snorm(static_cast<std::int8_t>(block[0]));
        const float r1 = snorm(static_cast<std::int8_t>(block[1]));
        palette[0] = r0;
        palette[1] = r1;
        if (static_cast<std::int8_t>(block[0]) > static_cast<std::int8_t>(block[1])) {
            for (int i = 1; i < 7; ++i) {
                palette[1 + i] = (static_cast<float>(7 - i) * r0 + static_cast<float>(i) * r1) / 7.0f;
            }
        } else {
            for (int i = 1; i < 5; ++i) {
                palette[1 + i] = (static_cast<float>(5 - i) * r0 + static_cast<float>(i) * r1) / 5.0f;
            }
            palette[6] = -1.0f;
            palette[7] = 1.0f;
        }
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    }
    for (int t = 0; t < 16; ++t) {
        out[t] = palette[(bits >> (3 * t)) & 7];
    }
}

}  // namespace

bool BcCodec::has_reference(DXGI_FORMAT format) {
    const int f = family(format);
    return f >= 1 && f <= 5;
}

int BcCodec::family(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return 1;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return 2;
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return 3;
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return 4;
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: return 5;
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: return 6;
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return 7;
    default: return 0;
    }
}

DXGI_FORMAT BcCodec::unorm_format(int f) {
    static const DXGI_FORMAT table[8] = {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM,
                                         DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC7_UNORM};
    return f >= 1 && f <= 7 ? table[f] : DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT BcCodec::srgb_format(int f) {
    switch (f) {
    case 1: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case 2: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case 3: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case 7: return DXGI_FORMAT_BC7_UNORM_SRGB;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT BcCodec::typeless_format(int f) {
    static const DXGI_FORMAT table[8] = {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC3_TYPELESS,
                                         DXGI_FORMAT_BC4_TYPELESS, DXGI_FORMAT_BC5_TYPELESS, DXGI_FORMAT_BC6H_TYPELESS,
                                         DXGI_FORMAT_BC7_TYPELESS};
    return f >= 1 && f <= 7 ? table[f] : DXGI_FORMAT_UNKNOWN;
}

void BcCodec::decode(int f, bool is_signed, const std::uint8_t* block, float texels[16][4]) {
    for (int t = 0; t < 16; ++t) {
        texels[t][0] = texels[t][1] = texels[t][2] = 0.0f;
        texels[t][3] = 1.0f;
    }
    switch (f) {
    case 1:
        color_block(block, false, texels, true);
        break;
    case 2: {
        color_block(block + 8, true, texels, false);
        for (int t = 0; t < 16; ++t) {
            const int nibble = (block[t / 2] >> (4 * (t % 2))) & 15;
            texels[t][3] = static_cast<float>(nibble * 17) / 255.0f;
        }
        break;
    }
    case 3: {
        color_block(block + 8, true, texels, false);
        float alpha[16];
        single_channel_block(block, false, alpha);
        for (int t = 0; t < 16; ++t) {
            texels[t][3] = alpha[t];
        }
        break;
    }
    case 4: {
        float red[16];
        single_channel_block(block, is_signed, red);
        for (int t = 0; t < 16; ++t) {
            texels[t][0] = red[t];
        }
        break;
    }
    case 5: {
        float red[16];
        float green[16];
        single_channel_block(block, is_signed, red);
        single_channel_block(block + 8, is_signed, green);
        for (int t = 0; t < 16; ++t) {
            texels[t][0] = red[t];
            texels[t][1] = green[t];
        }
        break;
    }
    default:
        break;
    }
}

std::vector<std::uint8_t> BcCodec::blocks(int f, std::uint64_t seed, int count) {
    const int size = block_bytes(f);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * static_cast<std::size_t>(count));
    Pattern::fill(seed, 0, data.data(), data.size());
    for (int b = 0; b < count; ++b) {
        std::uint8_t* block = data.data() + static_cast<std::size_t>(b) * static_cast<std::size_t>(size);
        std::uint8_t* color = f == 1 ? block : block + 8;
        if (f >= 1 && f <= 3) {
            std::uint16_t c0 = 0;
            std::uint16_t c1 = 0;
            std::memcpy(&c0, color, 2);
            std::memcpy(&c1, color + 2, 2);
            const bool want_four = f != 1 || (b % 3) != 2;
            if (want_four && c0 <= c1) {
                std::swap(c0, c1);
                if (c0 == c1) {
                    c0 = static_cast<std::uint16_t>(c1 + 1);
                }
            } else if (!want_four && c0 > c1) {
                std::swap(c0, c1);
            }
            std::memcpy(color, &c0, 2);
            std::memcpy(color + 2, &c1, 2);
        }
        if (f == 3 || f == 4 || f == 5) {
            const int channels = f == 5 ? 2 : 1;
            for (int c = 0; c < channels; ++c) {
                std::uint8_t* alpha = block + c * 8;
                const bool eight = ((b + c) % 2) == 0;
                if (eight && alpha[0] <= alpha[1]) {
                    std::swap(alpha[0], alpha[1]);
                    if (alpha[0] == alpha[1]) {
                        alpha[0] = static_cast<std::uint8_t>(alpha[1] + 1);
                    }
                } else if (!eight && alpha[0] > alpha[1]) {
                    std::swap(alpha[0], alpha[1]);
                }
            }
        }
    }
    return data;
}

double BcCodec::tolerance(int f, bool endpoint) {
    if (endpoint) {
        return 1.01 / 255.0;
    }
    return f <= 3 ? 3.01 / 255.0 : 2.01 / 255.0;
}

}  // namespace lsw::suites
