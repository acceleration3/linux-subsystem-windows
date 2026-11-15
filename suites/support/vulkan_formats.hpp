#pragma once

#include <cstdint>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites {
enum class VulkanFormatKind { Unorm, Uint, Float, Depth, Stencil };

struct VulkanFormatInfo {
    VkFormat format;
    std::uint32_t texel;
    std::uint32_t channels;
    VulkanFormatKind kind;
    bool bgra;
};

class VulkanFormats {
public:
    [[nodiscard]] static const std::vector<VulkanFormatInfo>& all();

    [[nodiscard]] static const VulkanFormatInfo* find(VkFormat format);

    [[nodiscard]] static std::vector<std::uint8_t> encode_clear(const VulkanFormatInfo& info, const VkClearColorValue& value);

    [[nodiscard]] static VkClearColorValue clear_value(const VulkanFormatInfo& info, std::uint32_t seed);

    [[nodiscard]] static VkImageAspectFlags aspect(const VulkanFormatInfo& info);
};
}  // namespace lsw::suites
