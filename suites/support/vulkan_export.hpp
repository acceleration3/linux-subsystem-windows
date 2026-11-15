#pragma once

#include "support/vulkan_device.hpp"

namespace lsw::suites {
class VulkanExport {
public:
    VulkanExport() = default;
    VulkanExport(const VulkanExport&) = delete;
    VulkanExport& operator=(const VulkanExport&) = delete;
    ~VulkanExport();

    [[nodiscard]] bool create(const LUID& luid, std::string& error);

    [[nodiscard]] bool exportable(std::string& detail) const;

    [[nodiscard]] bool export_memory(VkDeviceSize size, HANDLE& handle, std::string& error);

    [[nodiscard]] const std::uint8_t* device_uuid() const { return uuid_; }

private:
    VulkanDeviceState state_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    std::uint8_t uuid_[VK_UUID_SIZE] = {};
};
}  // namespace lsw::suites
