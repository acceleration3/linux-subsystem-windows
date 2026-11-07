#pragma once

#include "support/vulkan_rig.hpp"

namespace lsw::suites {
class VulkanBuffer {
public:
    VulkanBuffer() = default;
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept { swap(other); }
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            swap(other);
        }
        return *this;
    }
    ~VulkanBuffer() { destroy(); }

    [[nodiscard]] VkResult create(VulkanRig& rig, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
                                  VkMemoryPropertyFlags preferred = 0, bool map = true);

    [[nodiscard]] VkResult create_in_type(VulkanRig& rig, VkDeviceSize size, VkBufferUsageFlags usage, std::uint32_t type,
                                          bool map = true);

    void destroy();

    [[nodiscard]] VkResult flush(VkDeviceSize offset = 0, VkDeviceSize length = VK_WHOLE_SIZE);

    [[nodiscard]] VkResult invalidate(VkDeviceSize offset = 0, VkDeviceSize length = VK_WHOLE_SIZE);

    void publish() {
        if (!coherent()) {
            (void)flush();
        }
    }

    void acquire() {
        if (!coherent()) {
            (void)invalidate();
        }
    }

    [[nodiscard]] bool host_visible() const;

    [[nodiscard]] bool coherent() const;

    [[nodiscard]] std::uint8_t* bytes() const { return static_cast<std::uint8_t*>(mapped); }

    [[nodiscard]] std::uint32_t* words() const { return static_cast<std::uint32_t*>(mapped); }

    void fill_pattern(std::uint64_t seed);

    void fill_value(std::uint8_t value);

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    VkDeviceSize allocation = 0;
    VkMemoryRequirements requirements{};
    std::uint32_t type = 0;

private:
    void swap(VulkanBuffer& other) noexcept;

    VulkanRig* rig_ = nullptr;
};
}  // namespace lsw::suites
