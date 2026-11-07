#pragma once

#include "support/format_codec.hpp"
#include "support/vulkan_image.hpp"
#include "support/vulkan_image_io.hpp"

#include <cstdint>
#include <vector>

namespace lsw::suites::sanity::fmt {
class VulkanFormatIo {
public:
    using Bytes = std::vector<std::uint8_t>;

    [[nodiscard]] static bool image_ok(VulkanRig& rig, VkFormat format, VkImageUsageFlags usage, std::uint32_t width,
                                       std::uint32_t height);

    [[nodiscard]] static VkFormatFeatureFlags features(VulkanRig& rig, VkFormat format);

    [[nodiscard]] static VkResult make(VulkanRig& rig, VulkanImage& image, VkFormat format, std::uint32_t width, std::uint32_t height,
                                       VkImageUsageFlags usage, std::uint32_t mips = 1, std::uint32_t layers = 1);

    [[nodiscard]] static VkResult upload(VulkanRig& rig, const VulkanImage& image, const Bytes& data, std::uint32_t width,
                                         std::uint32_t height, std::uint32_t mip = 0, std::uint32_t layer = 0);

    [[nodiscard]] static VkResult download(VulkanRig& rig, const VulkanImage& image, std::size_t bytes, std::uint32_t width,
                                           std::uint32_t height, Bytes& out, std::uint32_t mip = 0, std::uint32_t layer = 0);

    [[nodiscard]] static VkResult clear(VulkanRig& rig, const VulkanImage& image, const VkClearColorValue& value);

    [[nodiscard]] static VkClearColorValue clear_value(const FormatSpec& spec, const double values[4]);

    [[nodiscard]] static std::size_t level_bytes(const VulkanImage& image, std::uint32_t mip);
};
}  // namespace lsw::suites::sanity::fmt
