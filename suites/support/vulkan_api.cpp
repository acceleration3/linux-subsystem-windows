#include "support/vulkan_api.hpp"

#include "core/text.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites {

std::uint64_t VulkanDevice::largest_device_local() const {
    std::uint64_t best = 0;
    for (const VulkanHeap& heap : heaps) {
        if (heap.device_local) {
            best = std::max(best, heap.size);
        }
    }
    return best;
}

std::uint64_t VulkanDevice::total_heaps() const {
    std::uint64_t total = 0;
    for (const VulkanHeap& heap : heaps) {
        total += heap.size;
    }
    return total;
}

std::uint64_t VulkanDevice::total_host_heaps() const {
    std::uint64_t total = 0;
    for (const VulkanHeap& heap : heaps) {
        if (!heap.device_local) {
            total += heap.size;
        }
    }
    return total;
}

bool VulkanApi::load(std::string& error) {
    if (!loader_.load("vulkan-1.dll")) {
        error = "vulkan-1.dll cannot be loaded";
        return false;
    }
    get_instance_proc_ = loader_.symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get_instance_proc_ == nullptr) {
        error = "vulkan-1.dll has no vkGetInstanceProcAddr";
        return false;
    }
    create_instance_ =
        reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_(nullptr, "vkCreateInstance"));
    enumerate_version_ = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        get_instance_proc_(nullptr, "vkEnumerateInstanceVersion"));
    if (create_instance_ == nullptr) {
        error = "the Vulkan loader does not resolve vkCreateInstance";
        return false;
    }
    return true;
}

bool VulkanApi::instance_version(std::uint32_t& version, std::string& error) const {
    if (enumerate_version_ == nullptr) {
        version = VK_API_VERSION_1_0;
        return true;
    }
    const VkResult result = enumerate_version_(&version);
    if (result != VK_SUCCESS) {
        error = "vkEnumerateInstanceVersion failed: " + std::to_string(result);
        return false;
    }
    return true;
}

bool VulkanApi::create_instance(std::string& error) {
    destroy_instance();
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (!instance_version(loader_version, error)) {
        return false;
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lsw-suites";
    app.apiVersion = loader_version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    const VkResult result = create_instance_(&info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        instance_ = nullptr;
        error = "vkCreateInstance failed: " + std::to_string(result);
        return false;
    }
    destroy_instance_ = reinterpret_cast<PFN_vkDestroyInstance>(
        get_instance_proc_(instance_, "vkDestroyInstance"));
    enumerate_devices_ = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get_instance_proc_(instance_, "vkEnumeratePhysicalDevices"));
    get_properties2_ = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        get_instance_proc_(instance_, "vkGetPhysicalDeviceProperties2"));
    get_memory_ = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        get_instance_proc_(instance_, "vkGetPhysicalDeviceMemoryProperties"));
    if (enumerate_devices_ == nullptr || get_properties2_ == nullptr || get_memory_ == nullptr) {
        error = "the instance does not resolve the physical device entry points";
        destroy_instance();
        return false;
    }
    return true;
}

void VulkanApi::destroy_instance() {
    if (instance_ != nullptr && destroy_instance_ != nullptr) {
        destroy_instance_(instance_, nullptr);
    }
    instance_ = nullptr;
}

bool VulkanApi::devices(std::vector<VulkanDevice>& out, std::string& error) const {
    if (instance_ == nullptr) {
        error = "no Vulkan instance";
        return false;
    }
    std::uint32_t count = 0;
    VkResult result = enumerate_devices_(instance_, &count, nullptr);
    if (result != VK_SUCCESS) {
        error = "vkEnumeratePhysicalDevices failed: " + std::to_string(result);
        return false;
    }
    std::vector<VkPhysicalDevice> handles(count);
    result = enumerate_devices_(instance_, &count, handles.data());
    if (result != VK_SUCCESS) {
        error = "vkEnumeratePhysicalDevices (fill) failed: " + std::to_string(result);
        return false;
    }
    handles.resize(count);
    for (VkPhysicalDevice handle : handles) {
        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &id;
        get_properties2_(handle, &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        get_memory_(handle, &memory);
        VulkanDevice device;
        const VkPhysicalDeviceProperties& base = properties.properties;
        device.name.assign(base.deviceName, ::strnlen(base.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE));
        device.name_terminated =
            std::memchr(base.deviceName, 0, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) != nullptr;
        device.vendor = base.vendorID;
        device.device = base.deviceID;
        device.api_version = base.apiVersion;
        device.driver_version = base.driverVersion;
        device.type = static_cast<std::uint32_t>(base.deviceType);
        device.luid_valid = id.deviceLUIDValid != VK_FALSE;
        if (device.luid_valid) {
            std::memcpy(&device.luid, id.deviceLUID, sizeof(device.luid));
        }
        for (std::uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
            device.heaps.push_back(
                {memory.memoryHeaps[h].size,
                 (memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0});
        }
        out.push_back(std::move(device));
    }
    return true;
}

bool VulkanApi::enumerate_fresh(std::vector<VulkanDevice>& out, std::string& error) {
    if (!create_instance(error)) {
        return false;
    }
    const bool ok = devices(out, error);
    destroy_instance();
    return ok;
}

}  // namespace lsw::suites
