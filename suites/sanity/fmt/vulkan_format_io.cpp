#include "sanity/fmt/vulkan_format_io.hpp"

#include "support/format_table.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites::sanity::fmt {

bool VulkanFormatIo::image_ok(VulkanRig& rig, VkFormat format, VkImageUsageFlags usage, std::uint32_t width, std::uint32_t height) {
    VkImageFormatProperties props{};
    const VkResult result = rig.fn.vkGetPhysicalDeviceImageFormatProperties(rig.physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                                           usage, 0, &props);
    return result == VK_SUCCESS && props.maxExtent.width >= width && props.maxExtent.height >= height;
}

VkFormatFeatureFlags VulkanFormatIo::features(VulkanRig& rig, VkFormat format) {
    VkFormatProperties props{};
    rig.fn.vkGetPhysicalDeviceFormatProperties(rig.physical, format, &props);
    return props.optimalTilingFeatures;
}

VkResult VulkanFormatIo::make(VulkanRig& rig, VulkanImage& image, VkFormat format, std::uint32_t width, std::uint32_t height,
                              VkImageUsageFlags usage, std::uint32_t mips, std::uint32_t layers) {
    const VkResult result = image.create_2d(rig, format, width, height, usage, VK_SAMPLE_COUNT_1_BIT, mips, layers);
    if (result != VK_SUCCESS) {
        return result;
    }
    return VulkanImageIo::to_general(rig, image);
}

VkResult VulkanFormatIo::upload(VulkanRig& rig, const VulkanImage& image, const Bytes& data, std::uint32_t width, std::uint32_t height,
                                std::uint32_t mip, std::uint32_t layer) {
    ImageRegion region;
    region.aspect = image.aspect();
    region.mip = mip;
    region.layer = layer;
    region.extent = {width, height, 1};
    return VulkanImageIo::write(rig, image, region, data);
}

VkResult VulkanFormatIo::download(VulkanRig& rig, const VulkanImage& image, std::size_t bytes, std::uint32_t width, std::uint32_t height,
                                  Bytes& out, std::uint32_t mip, std::uint32_t layer) {
    ImageRegion region;
    region.aspect = image.aspect();
    region.mip = mip;
    region.layer = layer;
    region.extent = {width, height, 1};
    return VulkanImageIo::read_raw(rig, image, region, bytes, out);
}

VkResult VulkanFormatIo::clear(VulkanRig& rig, const VulkanImage& image, const VkClearColorValue& value) {
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.info.mipLevels, 0, image.info.arrayLayers};
    const VkResult result = rig.run([&](VkCommandBuffer commands) {
        rig.fn.vkCmdClearColorImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                    nullptr);
    });
    return result;
}

VkClearColorValue VulkanFormatIo::clear_value(const FormatSpec& spec, const double values[4]) {
    VkClearColorValue value{};
    for (int i = 0; i < 4; ++i) {
        if (spec.kind == FormatKind::Uint) {
            value.uint32[i] = static_cast<std::uint32_t>(std::max(values[i], 0.0));
        } else if (spec.kind == FormatKind::Sint) {
            value.int32[i] = static_cast<std::int32_t>(values[i]);
        } else {
            value.float32[i] = static_cast<float>(values[i]);
        }
    }
    return value;
}

std::size_t VulkanFormatIo::level_bytes(const VulkanImage& image, std::uint32_t mip) {
    const std::uint32_t width = std::max(1u, image.info.extent.width >> mip);
    const std::uint32_t height = std::max(1u, image.info.extent.height >> mip);
    const FormatEntry* entry = nullptr;
    for (const FormatEntry& candidate : FormatTable::all()) {
        if (candidate.vulkan == static_cast<int>(image.info.format)) {
            entry = &candidate;
        }
    }
    if (entry == nullptr) {
        return 0;
    }
    const std::size_t element = static_cast<std::size_t>(FormatCodec::element_bytes(entry->dxgi));
    if (FormatCodec::is_block(entry->dxgi)) {
        return static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * element;
    }
    return static_cast<std::size_t>(width) * height * element;
}

}  // namespace lsw::suites::sanity::fmt
