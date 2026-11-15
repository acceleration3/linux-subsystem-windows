#pragma once

#include "support/vulkan_rig.hpp"

namespace lsw::suites {
class VulkanImage {
public:
    VulkanImage() = default;
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&& other) noexcept { swap(other); }
    VulkanImage& operator=(VulkanImage&& other) noexcept {
        if (this != &other) {
            destroy();
            swap(other);
        }
        return *this;
    }
    ~VulkanImage() { destroy(); }

    [[nodiscard]] VkResult create(VulkanRig& rig, const VkImageCreateInfo& info, VkMemoryPropertyFlags required = 0,
                                  VkMemoryPropertyFlags preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    [[nodiscard]] VkResult create_2d(VulkanRig& rig, VkFormat format, std::uint32_t width, std::uint32_t height,
                                     VkImageUsageFlags usage, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                                     std::uint32_t mips = 1, std::uint32_t layers = 1);

    [[nodiscard]] VkResult view(VulkanOwned<VkImageView>& out, VkImageViewType type, VkImageAspectFlags aspect,
                                std::uint32_t base_mip = 0, std::uint32_t mips = 1, std::uint32_t base_layer = 0,
                                std::uint32_t layers = 1) const;

    void destroy();

    [[nodiscard]] static VkImageMemoryBarrier barrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags source,
                                                      VkAccessFlags destination, VkImageAspectFlags aspect,
                                                      std::uint32_t mips = 1, std::uint32_t layers = 1);

    [[nodiscard]] VkImageAspectFlags aspect() const;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo info{};
    VkMemoryRequirements requirements{};

private:
    void swap(VulkanImage& other) noexcept;

    VulkanRig* rig_ = nullptr;
};
}  // namespace lsw::suites
