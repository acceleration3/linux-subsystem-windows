#include "support/vulkan_formats.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites {

namespace {

std::uint16_t half(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
    const std::uint32_t mantissa = (bits >> 13) & 0x3FFu;
    if (value == 0.0f) {
        return static_cast<std::uint16_t>(sign);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | mantissa);
}

void put(std::vector<std::uint8_t>& out, std::uint64_t value, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

}  // namespace

const std::vector<VulkanFormatInfo>& VulkanFormats::all() {
    static const std::vector<VulkanFormatInfo> table = {
        {VK_FORMAT_R8_UNORM, 1, 1, VulkanFormatKind::Unorm, false},
        {VK_FORMAT_R8G8_UNORM, 2, 2, VulkanFormatKind::Unorm, false},
        {VK_FORMAT_R8G8B8A8_UNORM, 4, 4, VulkanFormatKind::Unorm, false},
        {VK_FORMAT_B8G8R8A8_UNORM, 4, 4, VulkanFormatKind::Unorm, true},
        {VK_FORMAT_R8_UINT, 1, 1, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R16_UINT, 2, 1, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R16G16_UINT, 4, 2, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R16G16B16A16_UINT, 8, 4, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R32_UINT, 4, 1, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R32G32_UINT, 8, 2, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R32G32B32A32_UINT, 16, 4, VulkanFormatKind::Uint, false},
        {VK_FORMAT_R16_UNORM, 2, 1, VulkanFormatKind::Unorm, false},
        {VK_FORMAT_R16G16B16A16_UNORM, 8, 4, VulkanFormatKind::Unorm, false},
        {VK_FORMAT_R32_SFLOAT, 4, 1, VulkanFormatKind::Float, false},
        {VK_FORMAT_R16G16B16A16_SFLOAT, 8, 4, VulkanFormatKind::Float, false},
        {VK_FORMAT_R32G32B32A32_SFLOAT, 16, 4, VulkanFormatKind::Float, false},
        {VK_FORMAT_D16_UNORM, 2, 1, VulkanFormatKind::Depth, false},
        {VK_FORMAT_D32_SFLOAT, 4, 1, VulkanFormatKind::Depth, false},
        {VK_FORMAT_S8_UINT, 1, 1, VulkanFormatKind::Stencil, false},
    };
    return table;
}

const VulkanFormatInfo* VulkanFormats::find(VkFormat format) {
    for (const VulkanFormatInfo& info : all()) {
        if (info.format == format) {
            return &info;
        }
    }
    return nullptr;
}

VkImageAspectFlags VulkanFormats::aspect(const VulkanFormatInfo& info) {
    if (info.kind == VulkanFormatKind::Depth) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (info.kind == VulkanFormatKind::Stencil) {
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkClearColorValue VulkanFormats::clear_value(const VulkanFormatInfo& info, std::uint32_t seed) {
    VkClearColorValue value{};
    const std::uint32_t channel_bits = info.texel / info.channels * 8;
    for (std::uint32_t c = 0; c < 4; ++c) {
        const std::uint32_t step = (seed * 37 + c * 53 + 11) % 200 + 20;
        switch (info.kind) {
            case VulkanFormatKind::Unorm:
                value.float32[c] = channel_bits == 16 ? static_cast<float>(step * 251) / 65535.0f : static_cast<float>(step) / 255.0f;
                break;
            case VulkanFormatKind::Uint: {
                const std::uint32_t mask = channel_bits >= 32 ? 0xFFFFFFFFu : ((1u << channel_bits) - 1);
                value.uint32[c] = (step * 0x01010101u + seed * 0x9E3779B1u) & mask;
                break;
            }
            default:
                value.float32[c] = static_cast<float>(step) / 64.0f;
                break;
        }
    }
    return value;
}

std::vector<std::uint8_t> VulkanFormats::encode_clear(const VulkanFormatInfo& info, const VkClearColorValue& value) {
    std::vector<std::uint8_t> out;
    const std::uint32_t channel_bytes = info.texel / info.channels;
    for (std::uint32_t c = 0; c < info.channels; ++c) {
        const std::uint32_t source = info.bgra && c < 3 ? 2 - c : c;
        switch (info.kind) {
            case VulkanFormatKind::Unorm:
                put(out, static_cast<std::uint64_t>(std::lround(value.float32[source] * (channel_bytes == 1 ? 255.0f : 65535.0f))), channel_bytes);
                break;
            case VulkanFormatKind::Uint:
                put(out, value.uint32[source], channel_bytes);
                break;
            case VulkanFormatKind::Float:
                if (channel_bytes == 2) {
                    put(out, half(value.float32[source]), 2);
                } else {
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &value.float32[source], 4);
                    put(out, bits, 4);
                }
                break;
            default:
                break;
        }
    }
    return out;
}

}  // namespace lsw::suites
