#pragma once

#include "support/dyn_library.hpp"

#include <cstdint>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace lsw::suites {
struct VulkanHeap {
    std::uint64_t size = 0;
    bool device_local = false;
};

struct VulkanDevice {
    std::string name;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::uint32_t api_version = 0;
    std::uint32_t driver_version = 0;
    std::uint32_t type = 0;
    bool luid_valid = false;
    bool name_terminated = true;
    LUID luid{};
    std::vector<VulkanHeap> heaps;

    [[nodiscard]] std::uint64_t largest_device_local() const;

    [[nodiscard]] std::uint64_t total_heaps() const;

    [[nodiscard]] std::uint64_t total_host_heaps() const;
};

class VulkanApi {
public:
    VulkanApi() = default;
    VulkanApi(const VulkanApi&) = delete;
    VulkanApi& operator=(const VulkanApi&) = delete;
    ~VulkanApi() { destroy_instance(); }

    [[nodiscard]] bool load(std::string& error);

    [[nodiscard]] bool instance_version(std::uint32_t& version, std::string& error) const;

    [[nodiscard]] bool create_instance(std::string& error);

    void destroy_instance();

    [[nodiscard]] bool devices(std::vector<VulkanDevice>& out, std::string& error) const;

    [[nodiscard]] bool enumerate_fresh(std::vector<VulkanDevice>& out, std::string& error);

private:
    DynLibrary loader_;
    PFN_vkGetInstanceProcAddr get_instance_proc_ = nullptr;
    PFN_vkCreateInstance create_instance_ = nullptr;
    PFN_vkEnumerateInstanceVersion enumerate_version_ = nullptr;
    VkInstance instance_ = nullptr;
    PFN_vkDestroyInstance destroy_instance_ = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_devices_ = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 get_properties2_ = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties get_memory_ = nullptr;
};
}  // namespace lsw::suites
