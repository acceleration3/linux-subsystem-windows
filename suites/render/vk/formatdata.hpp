#pragma once

#include "render/common/image.hpp"
#include "render/common/pattern.hpp"
#include "render/vk/vk.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lsw::render::vk {

enum class Kind { Unorm, Snorm, Uscaled, Sscaled, Uint, Sint, Float, Float11 };

struct FormatEntry {
    const char* name;
    VkFormat format;
    char cls;
    unsigned bytes;
    Kind kind;
    unsigned bits[4];
};


inline void make_scale(const FormatEntry& entry, float scale[4], float bias[4]) {
    for (int c = 0; c < 4; ++c) {
        const double bits = entry.bits[c] != 0 ? entry.bits[c] : 8;
        const double umax = std::pow(2.0, bits) - 1.0;
        const double smax = std::pow(2.0, bits - 1.0);
        switch (entry.kind) {
            case Kind::Unorm:
            case Kind::Float:
            case Kind::Float11:
                scale[c] = 1.0f;
                bias[c] = 0.0f;
                break;
            case Kind::Snorm:
                scale[c] = 0.5f;
                bias[c] = 0.5f;
                break;
            case Kind::Uscaled:
            case Kind::Uint:
                scale[c] = static_cast<float>(1.0 / umax);
                bias[c] = 0.0f;
                break;
            case Kind::Sscaled:
            case Kind::Sint:
                scale[c] = static_cast<float>(0.5 / smax);
                bias[c] = 0.5f;
                break;
        }
    }
}

inline std::vector<std::uint8_t> make_format_bytes(const FormatEntry& entry, unsigned count) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) * entry.bytes);
    std::uint32_t state = 0x1234567u + entry.bytes * 977u;
    if (entry.kind == Kind::Float11) {
        for (unsigned v = 0; v < count; ++v) {
            std::uint32_t packed = 0;
            const unsigned shifts[3] = {0, 11, 22};
            const unsigned widths[3] = {11, 11, 10};
            for (unsigned c = 0; c < 3; ++c) {
                const std::uint16_t half = PixelInfo::float_to_half(0.05f + 0.9f * Pattern::unit_random(state));
                const std::uint32_t field = (half >> (15 - widths[c])) & ((1u << widths[c]) - 1u);
                packed |= field << shifts[c];
            }
            std::memcpy(bytes.data() + static_cast<std::size_t>(v) * 4, &packed, 4);
        }
        return bytes;
    }
    if (entry.kind == Kind::Float) {
        unsigned comps = 0;
        while (comps < 4 && entry.bits[comps] != 0) {
            ++comps;
        }
        for (unsigned v = 0; v < count; ++v) {
            for (unsigned c = 0; c < comps; ++c) {
                const float value = 0.1f + 0.8f * Pattern::unit_random(state);
                std::uint8_t* at = bytes.data() + static_cast<std::size_t>(v) * entry.bytes;
                if (entry.bits[c] == 16) {
                    const std::uint16_t half = PixelInfo::float_to_half(value);
                    std::memcpy(at + c * 2, &half, 2);
                } else {
                    std::memcpy(at + c * 4, &value, 4);
                }
            }
        }
        return bytes;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = static_cast<std::uint8_t>(state >> 24);
    }
    return bytes;
}


}  // namespace lsw::render::vk
