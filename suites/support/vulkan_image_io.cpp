#include "support/vulkan_image_io.hpp"

#include "support/vulkan_buffer.hpp"

#include <cstring>

namespace lsw::suites {

namespace {

VkBufferImageCopy copy_region(const ImageRegion& region) {
    VkBufferImageCopy copy{};
    copy.imageSubresource = {region.aspect, region.mip, region.layer, region.layers};
    copy.imageOffset = region.offset;
    copy.imageExtent = region.extent;
    return copy;
}

}  // namespace

VkResult VulkanImageIo::to_general(VulkanRig& rig, const VulkanImage& image) {
    return rig.run([&](VkCommandBuffer commands) {
        const VkImageMemoryBarrier barrier = VulkanImage::barrier(
            image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            image.aspect(), image.info.mipLevels, image.info.arrayLayers);
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                                    nullptr, 1, &barrier);
    });
}

VkResult VulkanImageIo::write(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region,
                              const std::vector<std::uint8_t>& data) {
    VulkanBuffer staging;
    VkResult result = staging.create(rig, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::memcpy(staging.mapped, data.data(), data.size());
    staging.publish();
    const VkBufferImageCopy copy = copy_region(region);
    return rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdCopyBufferToImage(commands, staging.buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0,
                                    nullptr, 0, nullptr);
    });
}

VkResult VulkanImageIo::read(VulkanRig& rig, const VulkanImage& image, const ImageRegion& region, std::vector<std::uint8_t>& data,
                             std::uint32_t texel) {
    const std::size_t bytes = region_bytes(region, texel);
    VulkanBuffer staging;
    VkResult result = staging.create(rig, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::memset(staging.mapped, 0xCD, bytes);
    staging.publish();
    const VkBufferImageCopy copy = copy_region(region);
    result = rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdCopyImageToBuffer(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, staging.buffer, 1, &copy);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                    nullptr, 0, nullptr);
    });
    if (result != VK_SUCCESS) {
        return result;
    }
    staging.acquire();
    data.assign(staging.bytes(), staging.bytes() + bytes);
    return VK_SUCCESS;
}

}  // namespace lsw::suites
