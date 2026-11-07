#include "support/vulkan_buffer.hpp"

#include "support/pattern.hpp"

#include <cstring>
#include <utility>

namespace lsw::suites {

void VulkanBuffer::swap(VulkanBuffer& other) noexcept {
    std::swap(rig_, other.rig_);
    std::swap(buffer, other.buffer);
    std::swap(memory, other.memory);
    std::swap(mapped, other.mapped);
    std::swap(size, other.size);
    std::swap(allocation, other.allocation);
    std::swap(requirements, other.requirements);
    std::swap(type, other.type);
}

VkResult VulkanBuffer::create(VulkanRig& rig, VkDeviceSize byte_size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
                              VkMemoryPropertyFlags preferred, bool map) {
    destroy();
    rig_ = &rig;
    size = byte_size;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = byte_size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = rig.fn.vkCreateBuffer(rig.device, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return result;
    }
    rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer, &requirements);
    const int found = rig.find_type(requirements.memoryTypeBits, required, preferred);
    if (found < 0) {
        destroy();
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return create_in_type(rig, byte_size, usage, static_cast<std::uint32_t>(found), map);
}

VkResult VulkanBuffer::create_in_type(VulkanRig& rig, VkDeviceSize byte_size, VkBufferUsageFlags usage, std::uint32_t memory_type,
                                      bool map) {
    if (buffer == VK_NULL_HANDLE) {
        rig_ = &rig;
        size = byte_size;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = byte_size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VkResult created = rig.fn.vkCreateBuffer(rig.device, &info, nullptr, &buffer);
        if (created != VK_SUCCESS) {
            buffer = VK_NULL_HANDLE;
            return created;
        }
        rig.fn.vkGetBufferMemoryRequirements(rig.device, buffer, &requirements);
    }
    type = memory_type;
    allocation = requirements.size;
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type;
    VkResult result = rig.fn.vkAllocateMemory(rig.device, &allocate, nullptr, &memory);
    if (result != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        destroy();
        return result;
    }
    result = rig.fn.vkBindBufferMemory(rig.device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        destroy();
        return result;
    }
    if (map && host_visible()) {
        result = rig.fn.vkMapMemory(rig.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (result != VK_SUCCESS) {
            mapped = nullptr;
            destroy();
            return result;
        }
    }
    return VK_SUCCESS;
}

void VulkanBuffer::destroy() {
    if (rig_ != nullptr) {
        rig_->settle();
        if (mapped != nullptr) {
            rig_->fn.vkUnmapMemory(rig_->device, memory);
        }
        if (buffer != VK_NULL_HANDLE) {
            rig_->fn.vkDestroyBuffer(rig_->device, buffer, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            rig_->fn.vkFreeMemory(rig_->device, memory, nullptr);
        }
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
    size = 0;
    allocation = 0;
}

bool VulkanBuffer::host_visible() const {
    return rig_ != nullptr && (rig_->memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

bool VulkanBuffer::coherent() const {
    return rig_ != nullptr && (rig_->memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

VkResult VulkanBuffer::flush(VkDeviceSize offset, VkDeviceSize length) {
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = offset;
    range.size = length;
    return rig_->fn.vkFlushMappedMemoryRanges(rig_->device, 1, &range);
}

VkResult VulkanBuffer::invalidate(VkDeviceSize offset, VkDeviceSize length) {
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = offset;
    range.size = length;
    return rig_->fn.vkInvalidateMappedMemoryRanges(rig_->device, 1, &range);
}

void VulkanBuffer::fill_pattern(std::uint64_t seed) {
    Pattern::fill(seed, 0, mapped, static_cast<std::size_t>(size));
}

void VulkanBuffer::fill_value(std::uint8_t value) {
    std::memset(mapped, value, static_cast<std::size_t>(size));
}

}  // namespace lsw::suites
