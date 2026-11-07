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
};

class VulkanImageIo {
public:
    [[nodiscard]] static VkResult to_general(VulkanRig& rig, const VulkanImage& image);

    [[nodiscard]] static VkResult write(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region,
                                        const std::vector<std::uint8_t>& data);

    [[nodiscard]] static VkResult read(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region,
                                       std::vector<std::uint8_t>& data, std::uint32_t texel);

    [[nodiscard]] static std::size_t region_bytes(const ImageRegion& region, std::uint32_t texel) {
        return static_cast<std::size_t>(region.extent.width) * region.extent.height * region.extent.depth * region.layers * texel;
    }
};
}  // namespace lsw::suites
