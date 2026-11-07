#include "support/vulkan_device.hpp"

#include <cstring>

namespace lsw::suites {

VulkanDeviceState::~VulkanDeviceState() {
    if (device != nullptr && destroy_device != nullptr) {
        destroy_device(device, nullptr);
    }
    if (instance != nullptr && destroy_instance != nullptr) {
        destroy_instance(instance, nullptr);
    }
}

bool VulkanDeviceState::create(const LUID& luid, std::string& error) {
    if (!loader.load("vulkan-1.dll")) {
        error = "vulkan-1.dll cannot be loaded";
        return false;
    }
    get_instance_proc = loader.symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get_instance_proc == nullptr) {
        error = "vulkan-1.dll has no vkGetInstanceProcAddr";
        return false;
    }
    const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc(nullptr, "vkCreateInstance"));
    const auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        get_instance_proc(nullptr, "vkEnumerateInstanceVersion"));
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (enumerate_version != nullptr) {
        enumerate_version(&loader_version);
    }
    if (loader_version < VK_API_VERSION_1_1) {
        error = "the Vulkan loader is older than 1.1";
        return false;
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    if (create_instance == nullptr || create_instance(&info, nullptr, &instance) != VK_SUCCESS) {
        error = "vkCreateInstance failed";
        instance = nullptr;
        return false;
    }
#define LSW_VK_INSTANCE(field, type, name) field = reinterpret_cast<type>(get_instance_proc(instance, name))
    LSW_VK_INSTANCE(destroy_instance, PFN_vkDestroyInstance, "vkDestroyInstance");
    LSW_VK_INSTANCE(get_device_proc, PFN_vkGetDeviceProcAddr, "vkGetDeviceProcAddr");
    LSW_VK_INSTANCE(get_memory2, PFN_vkGetPhysicalDeviceMemoryProperties2, "vkGetPhysicalDeviceMemoryProperties2");
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_instance_proc(instance, "vkEnumeratePhysicalDevices"));
    const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get_instance_proc(instance, "vkGetPhysicalDeviceProperties2"));
    const auto memory_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get_instance_proc(instance, "vkGetPhysicalDeviceMemoryProperties"));
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get_instance_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(get_instance_proc(instance, "vkEnumerateDeviceExtensionProperties"));
    const auto create_device = reinterpret_cast<PFN_vkCreateDevice>(get_instance_proc(instance, "vkCreateDevice"));
    if (enumerate == nullptr || properties2 == nullptr || memory_properties == nullptr || families == nullptr ||
        extensions == nullptr || create_device == nullptr || get_device_proc == nullptr) {
        error = "the Vulkan instance does not resolve the device entry points";
        return false;
    }
    std::uint32_t count = 0;
    enumerate(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    enumerate(instance, &count, devices.data());
    VkPhysicalDeviceMaintenance3Properties maintenance{};
    for (std::uint32_t i = 0; i < count && physical == nullptr; ++i) {
        VkPhysicalDeviceMaintenance3Properties m3{};
        m3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        id.pNext = &m3;
        VkPhysicalDeviceProperties2 props{};
        props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props.pNext = &id;
        properties2(devices[i], &props);
        if (id.deviceLUIDValid != VK_FALSE && std::memcmp(id.deviceLUID, &luid, sizeof(luid)) == 0) {
            physical = devices[i];
            maintenance = m3;
            min_map_alignment = props.properties.limits.minMemoryMapAlignment;
        }
    }
    if (physical == nullptr) {
        error = "no Vulkan physical device carries the adapter's LUID";
        return false;
    }
    max_allocation_size = maintenance.maxMemoryAllocationSize;
    memory_properties(physical, &memory);
    std::uint32_t family_count = 0;
    families(physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> family_list(family_count);
    families(physical, &family_count, family_list.data());
    bool found = false;
    for (std::uint32_t i = 0; i < family_count && !found; ++i) {
        if ((family_list[i].queueFlags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0) {
            family = i;
            found = true;
        }
    }
    if (!found) {
        error = "no Vulkan queue family supports transfers";
        return false;
    }
    std::uint32_t extension_count = 0;
    extensions(physical, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extension_list(extension_count);
    extensions(physical, nullptr, &extension_count, extension_list.data());
    for (const VkExtensionProperties& extension : extension_list) {
        budget_extension = budget_extension || std::strcmp(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
    }
    bool win32_memory = false;
    for (const VkExtensionProperties& extension : extension_list) {
        win32_memory = win32_memory || std::strcmp(extension.extensionName, "VK_KHR_external_memory_win32") == 0;
    }
    external_win32 = win32_memory;
    LSW_VK_INSTANCE(image_format_properties2, PFN_vkGetPhysicalDeviceImageFormatProperties2, "vkGetPhysicalDeviceImageFormatProperties2");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    std::vector<const char*> enabled;
    if (budget_extension) {
        enabled.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }
    if (external_win32) {
        enabled.push_back("VK_KHR_external_memory_win32");
    }
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    device_info.ppEnabledExtensionNames = enabled.data();
    const VkResult result = create_device(physical, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        error = "vkCreateDevice failed: " + std::to_string(result);
        device = nullptr;
        return false;
    }
#define LSW_VK_DEVICE(field, type, name) field = reinterpret_cast<type>(get_device_proc(device, name))
    LSW_VK_DEVICE(destroy_device, PFN_vkDestroyDevice, "vkDestroyDevice");
    LSW_VK_DEVICE(get_queue, PFN_vkGetDeviceQueue, "vkGetDeviceQueue");
    LSW_VK_DEVICE(create_pool, PFN_vkCreateCommandPool, "vkCreateCommandPool");
    LSW_VK_DEVICE(destroy_pool, PFN_vkDestroyCommandPool, "vkDestroyCommandPool");
    LSW_VK_DEVICE(allocate_commands, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
    LSW_VK_DEVICE(begin_commands, PFN_vkBeginCommandBuffer, "vkBeginCommandBuffer");
    LSW_VK_DEVICE(end_commands, PFN_vkEndCommandBuffer, "vkEndCommandBuffer");
    LSW_VK_DEVICE(reset_commands, PFN_vkResetCommandBuffer, "vkResetCommandBuffer");
    LSW_VK_DEVICE(cmd_copy, PFN_vkCmdCopyBuffer, "vkCmdCopyBuffer");
    LSW_VK_DEVICE(queue_submit, PFN_vkQueueSubmit, "vkQueueSubmit");
    LSW_VK_DEVICE(queue_wait, PFN_vkQueueWaitIdle, "vkQueueWaitIdle");
    LSW_VK_DEVICE(create_buffer, PFN_vkCreateBuffer, "vkCreateBuffer");
    LSW_VK_DEVICE(destroy_buffer, PFN_vkDestroyBuffer, "vkDestroyBuffer");
    LSW_VK_DEVICE(buffer_requirements, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    LSW_VK_DEVICE(allocate_memory, PFN_vkAllocateMemory, "vkAllocateMemory");
    LSW_VK_DEVICE(free_memory, PFN_vkFreeMemory, "vkFreeMemory");
    LSW_VK_DEVICE(bind_buffer, PFN_vkBindBufferMemory, "vkBindBufferMemory");
    LSW_VK_DEVICE(map_memory, PFN_vkMapMemory, "vkMapMemory");
    LSW_VK_DEVICE(unmap_memory, PFN_vkUnmapMemory, "vkUnmapMemory");
    LSW_VK_DEVICE(flush_ranges, PFN_vkFlushMappedMemoryRanges, "vkFlushMappedMemoryRanges");
    LSW_VK_DEVICE(invalidate_ranges, PFN_vkInvalidateMappedMemoryRanges, "vkInvalidateMappedMemoryRanges");
    LSW_VK_DEVICE(device_wait, PFN_vkDeviceWaitIdle, "vkDeviceWaitIdle");
    LSW_VK_DEVICE(create_image, PFN_vkCreateImage, "vkCreateImage");
    LSW_VK_DEVICE(destroy_image, PFN_vkDestroyImage, "vkDestroyImage");
    LSW_VK_DEVICE(image_requirements, PFN_vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
    LSW_VK_DEVICE(bind_image, PFN_vkBindImageMemory, "vkBindImageMemory");
    LSW_VK_DEVICE(cmd_copy_image, PFN_vkCmdCopyImageToBuffer, "vkCmdCopyImageToBuffer");
    LSW_VK_DEVICE(cmd_barrier, PFN_vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
    get_queue(device, family, 0, &queue);
    return true;
}

int VulkanDeviceState::find_memory_type(std::uint32_t bits, VkMemoryPropertyFlags required,
                                        VkMemoryPropertyFlags preferred) const {
    int fallback = -1;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) == 0 || (flags & required) != required) {
            continue;
        }
        if ((flags & preferred) == preferred) {
            return static_cast<int>(i);
        }
        if (fallback < 0) {
            fallback = static_cast<int>(i);
        }
    }
    return fallback;
}

bool VulkanDeviceState::budget(std::vector<VkDeviceSize>& usage, std::vector<VkDeviceSize>& limit) const {
    if (!budget_extension || get_memory2 == nullptr) {
        return false;
    }
    VkPhysicalDeviceMemoryBudgetPropertiesEXT info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    props.pNext = &info;
    get_memory2(physical, &props);
    usage.assign(info.heapUsage, info.heapUsage + memory.memoryHeapCount);
    limit.assign(info.heapBudget, info.heapBudget + memory.memoryHeapCount);
    return true;
}

}  // namespace lsw::suites
