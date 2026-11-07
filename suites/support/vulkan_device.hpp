#pragma once

#include "support/dyn_library.hpp"

#include <mutex>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites {
class VulkanDeviceState {
public:
    VulkanDeviceState() = default;
    VulkanDeviceState(const VulkanDeviceState&) = delete;
    VulkanDeviceState& operator=(const VulkanDeviceState&) = delete;
    ~VulkanDeviceState();

    [[nodiscard]] bool create(const LUID& luid, std::string& error);

    [[nodiscard]] int find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required,
                                       VkMemoryPropertyFlags preferred) const;

    [[nodiscard]] bool budget(std::vector<VkDeviceSize>& usage, std::vector<VkDeviceSize>& limit) const;

    DynLibrary loader;
    VkInstance instance = nullptr;
    VkPhysicalDevice physical = nullptr;
    VkDevice device = nullptr;
    VkQueue queue = nullptr;
    std::uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memory{};
    VkDeviceSize max_allocation_size = 0;
    VkDeviceSize min_map_alignment = 0;
    bool budget_extension = false;
    std::mutex mutex;

    PFN_vkGetInstanceProcAddr get_instance_proc = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2 get_memory2 = nullptr;
    PFN_vkGetDeviceQueue get_queue = nullptr;
    PFN_vkCreateCommandPool create_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_pool = nullptr;
    PFN_vkAllocateCommandBuffers allocate_commands = nullptr;
    PFN_vkBeginCommandBuffer begin_commands = nullptr;
    PFN_vkEndCommandBuffer end_commands = nullptr;
    PFN_vkResetCommandBuffer reset_commands = nullptr;
    PFN_vkCmdCopyBuffer cmd_copy = nullptr;
    PFN_vkQueueSubmit queue_submit = nullptr;
    PFN_vkQueueWaitIdle queue_wait = nullptr;
    PFN_vkCreateBuffer create_buffer = nullptr;
    PFN_vkDestroyBuffer destroy_buffer = nullptr;
    PFN_vkGetBufferMemoryRequirements buffer_requirements = nullptr;
    PFN_vkAllocateMemory allocate_memory = nullptr;
    PFN_vkFreeMemory free_memory = nullptr;
    PFN_vkBindBufferMemory bind_buffer = nullptr;
    PFN_vkMapMemory map_memory = nullptr;
    PFN_vkUnmapMemory unmap_memory = nullptr;
    PFN_vkFlushMappedMemoryRanges flush_ranges = nullptr;
    PFN_vkInvalidateMappedMemoryRanges invalidate_ranges = nullptr;
    PFN_vkDeviceWaitIdle device_wait = nullptr;
};
}  // namespace lsw::suites
