#include "sanity/vk/kit.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace lsw::suites::sanity::vk {

Outcome Kit::finish(const std::vector<std::string>& skips, const Problems& problems) {
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (!skips.empty()) {
        std::string reason;
        for (const std::string& item : skips) {
            reason += (reason.empty() ? "" : "; ") + item;
        }
        return Context::skip(reason);
    }
    return Outcome::ok();
}

Outcome Kit::each_device(Context& context, const VulkanRigOptions& options, const DeviceBody& body) {
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    Problems problems;
    std::vector<std::string> skips;
    std::size_t exercised = 0;
    for (const AvAdapter& adapter : adapters) {
        VulkanRig rig;
        if (!rig.open(adapter, options, error)) {
            problems.add(adapter.name + ": " + error);
            continue;
        }
        Problems device_problems;
        const std::string skipped = body(rig, device_problems);
        rig.wait_idle();
        if (!skipped.empty()) {
            skips.push_back(skipped);
        } else {
            ++exercised;
        }
        if (!device_problems.empty()) {
            problems.add(rig.describe() + ": " + device_problems.text());
        }
    }
    context.measure("vk.devices", adapters.size());
    if (exercised != 0 && problems.empty()) {
        return Outcome::ok();
    }
    return finish(skips, problems);
}

bool Kit::expect(Problems& problems, const std::string& what, VkResult got, std::initializer_list<VkResult> accepted) {
    if (VulkanTools::code_in(got, accepted)) {
        return true;
    }
    problems.add(what + " returned " + name(got) + ", expected " + VulkanTools::accepted_text(accepted));
    return false;
}

VkResult Kit::host_buffer(VulkanRig& rig, VulkanBuffer& out, VkDeviceSize size, VkBufferUsageFlags usage) {
    return out.create(rig, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

VkResult Kit::device_buffer(VulkanRig& rig, VulkanBuffer& out, VkDeviceSize size, VkBufferUsageFlags usage) {
    return out.create(rig, size, usage, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
}

VkResult Kit::copy(VulkanRig& rig, VkBuffer source, VkBuffer destination, VkDeviceSize size, VkDeviceSize source_offset,
                   VkDeviceSize destination_offset) {
    return rig.run([&](VkCommandBuffer commands) {
        const VkBufferCopy region{source_offset, destination_offset, size};
        rig.fn.vkCmdCopyBuffer(commands, source, destination, 1, &region);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                    nullptr, 0, nullptr);
    });
}

std::vector<std::uint32_t> Kit::words(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint32_t> data(count);
    Pattern::fill(seed, 0, data.data(), count * sizeof(std::uint32_t));
    return data;
}

bool Kit::same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected, const void* actual,
                     std::size_t size) {
    if (size == 0 || std::memcmp(expected, actual, size) == 0) {
        return true;
    }
    const auto* a = static_cast<const std::uint8_t*>(expected);
    const auto* b = static_cast<const std::uint8_t*>(actual);
    std::size_t at = 0;
    while (at < size && a[at] == b[at]) {
        ++at;
    }
    problems.add(what + ": byte " + std::to_string(at) + " is 0x" + hex(b[at], 2) + ", expected 0x" + hex(a[at], 2));
    ArtifactWriter writer(context);
    const std::string base = sanitize(what);
    writer.bytes(base + ".expected", expected, size);
    writer.bytes(base + ".actual", actual, size);
    return false;
}

bool Kit::same_words(Context& context, Problems& problems, const std::string& what, const std::vector<std::uint32_t>& expected,
                     const std::vector<std::uint32_t>& actual) {
    if (expected.size() != actual.size()) {
        problems.add(what + ": " + std::to_string(actual.size()) + " words, expected " + std::to_string(expected.size()));
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            problems.add(what + ": word " + std::to_string(i) + " is 0x" + hex(actual[i]) + ", expected 0x" + hex(expected[i]));
            ArtifactWriter writer(context);
            const std::string base = sanitize(what);
            writer.bytes(base + ".expected", expected.data(), expected.size() * 4);
            writer.bytes(base + ".actual", actual.data(), actual.size() * 4);
            return false;
        }
    }
    return true;
}

VkCommandBuffer Kit::record(VulkanRig& rig, VulkanQueue& queue, const std::function<void(VkCommandBuffer)>& body, VkCommandBufferUsageFlags flags,
                            VkResult* code) {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = queue.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = nullptr;
    VkResult result = rig.fn.vkAllocateCommandBuffers(rig.device, &allocate, &commands);
    if (result == VK_SUCCESS) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = flags;
        result = rig.fn.vkBeginCommandBuffer(commands, &begin);
        if (result == VK_SUCCESS) {
            body(commands);
            result = rig.fn.vkEndCommandBuffer(commands);
        }
    }
    if (code != nullptr) {
        *code = result;
    }
    return result == VK_SUCCESS ? commands : nullptr;
}

void Kit::transfer_barrier(VulkanRig& rig, VkCommandBuffer commands, VkPipelineStageFlags source, VkAccessFlags source_access,
                           VkPipelineStageFlags destination, VkAccessFlags destination_access) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    rig.fn.vkCmdPipelineBarrier(commands, source, destination, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

std::string Kit::sanitize(std::string text) {
    for (char& c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-')) {
            c = '_';
        }
    }
    return text;
}

VkFormatProperties Kit::format_properties(VulkanRig& rig, VkFormat format) {
    VkFormatProperties out{};
    rig.fn.vkGetPhysicalDeviceFormatProperties(rig.physical, format, &out);
    return out;
}

bool Kit::image_supported(VulkanRig& rig, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
                          VkImageCreateFlags flags, VkImageFormatProperties& out, VkResult* code) {
    const VkResult result = rig.fn.vkGetPhysicalDeviceImageFormatProperties(rig.physical, format, type, tiling, usage, flags, &out);
    if (code != nullptr) {
        *code = result;
    }
    return result == VK_SUCCESS;
}

std::uint64_t Kit::device_local_budget(VulkanRig& rig) {
    std::uint64_t best = 0;
    for (std::uint32_t h = 0; h < rig.memory.memoryHeapCount; ++h) {
        if ((rig.memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            best = std::max(best, rig.heap_budget(h));
        }
    }
    return best;
}

std::uint32_t Kit::buffer_type_bits(VulkanRig& rig, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanOwned<VkBuffer> buffer;
    if (rig.make(buffer, rig.fn.vkCreateBuffer, rig.fn.vkDestroyBuffer, info) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements requirements{};
    rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer.get(), &requirements);
    return requirements.memoryTypeBits;
}

std::uint32_t Kit::image_type_bits(VulkanRig& rig, VkFormat format, std::uint32_t width, std::uint32_t height,
                                   VkImageUsageFlags usage) {
    VulkanOwned<VkImage> image;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (rig.make(image, rig.fn.vkCreateImage, rig.fn.vkDestroyImage, info) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements requirements{};
    rig.fn.vkGetImageMemoryRequirements(rig.device, image.get(), &requirements);
    return requirements.memoryTypeBits;
}

}  // namespace lsw::suites::sanity::vk
