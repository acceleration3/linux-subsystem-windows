#pragma once

#include "support/vulkan_image.hpp"

#include <vector>

namespace lsw::suites {
struct ImageRegion {
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    std::uint32_t layers = 1;
    VkOffset3D offset{0, 0, 0};
    VkExtent3D extent{1, 1, 1};
    std::uint32_t buffer_offset = 0;
    std::uint32_t row_length = 0;
    std::uint32_t image_height = 0;
};

class VulkanImageIo {
public:
    [[nodiscard]] static VkResult to_general(VulkanRig& rig, const VulkanImage& image);

    [[nodiscard]] static VkResult write(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region,
                                        const std::vector<std::uint8_t>& data);

    [[nodiscard]] static VkResult read(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region,
                                       std::vector<std::uint8_t>& data, std::uint32_t texel);

    [[nodiscard]] static std::size_t span_bytes(const ImageRegion& region, std::uint32_t texel) {
        const std::size_t row = region.row_length != 0 ? region.row_length : region.extent.width;
        const std::size_t high = region.image_height != 0 ? region.image_height : region.extent.height;
        const std::size_t slices = static_cast<std::size_t>(region.extent.depth) * region.layers;
        return region.buffer_offset + (((slices - 1) * high + (region.extent.height - 1)) * row + region.extent.width) * texel;
    }

    [[nodiscard]] static std::size_t region_bytes(const ImageRegion& region, std::uint32_t texel) {
        return static_cast<std::size_t>(region.extent.width) * region.extent.height * region.extent.depth * region.layers * texel;
    }
};
}  // namespace lsw::suites
