#include "support/vulkan_image.hpp"

#include <utility>

namespace lsw::suites {

void VulkanImage::swap(VulkanImage& other) noexcept {
    std::swap(rig_, other.rig_);
    std::swap(image, other.image);
    std::swap(memory, other.memory);
    std::swap(info, other.info);
    std::swap(requirements, other.requirements);
}

VkResult VulkanImage::create(VulkanRig& rig, const VkImageCreateInfo& create_info, VkMemoryPropertyFlags required,
                             VkMemoryPropertyFlags preferred) {
    destroy();
    rig_ = &rig;
    info = create_info;
    info.pNext = nullptr;
    VkResult result = rig.fn.vkCreateImage(rig.device, &create_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        image = VK_NULL_HANDLE;
        return result;
    }
    rig.fn.vkGetImageMemoryRequirements(rig.device, image, &requirements);
    const int type = rig.find_type(requirements.memoryTypeBits, required, preferred);
    if (type < 0) {
        destroy();
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
    result = rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        destroy();
        return result;
    }
    result = rig.fn.vkBindImageMemory(rig.device, image, memory, 0);
    if (result != VK_SUCCESS) {
        destroy();
    }
    return result;
}

VkResult VulkanImage::create_2d(VulkanRig& rig, VkFormat format, std::uint32_t width, std::uint32_t height,
                                VkImageUsageFlags usage, VkSampleCountFlagBits samples, std::uint32_t mips,
                                std::uint32_t layers) {
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = format;
    create_info.extent = {width, height, 1};
    create_info.mipLevels = mips;
    create_info.arrayLayers = layers;
    create_info.samples = samples;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = usage;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return create(rig, create_info);
}

VkResult VulkanImage::view(VulkanOwned<VkImageView>& out, VkImageViewType type, VkImageAspectFlags aspect_mask,
                           std::uint32_t base_mip, std::uint32_t mips, std::uint32_t base_layer, std::uint32_t layers) const {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = type;
    view_info.format = info.format;
    view_info.subresourceRange = {aspect_mask, base_mip, mips, base_layer, layers};
    return rig_->make(out, rig_->fn.vkCreateImageView, rig_->fn.vkDestroyImageView, view_info);
}

void VulkanImage::destroy() {
    if (rig_ != nullptr) {
        rig_->settle();
        if (image != VK_NULL_HANDLE) {
            rig_->fn.vkDestroyImage(rig_->device, image, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            rig_->fn.vkFreeMemory(rig_->device, memory, nullptr);
        }
    }
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

VkImageAspectFlags VulkanImage::aspect() const {
    switch (info.format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

VkImageMemoryBarrier VulkanImage::barrier(VkImage target, VkImageLayout from, VkImageLayout to, VkAccessFlags source,
                                          VkAccessFlags destination, VkImageAspectFlags aspect_mask, std::uint32_t mips,
                                          std::uint32_t layers) {
    VkImageMemoryBarrier value{};
    value.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    value.srcAccessMask = source;
    value.dstAccessMask = destination;
    value.oldLayout = from;
    value.newLayout = to;
    value.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    value.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    value.image = target;
    value.subresourceRange = {aspect_mask, 0, mips, 0, layers};
    return value;
}

}  // namespace lsw::suites
