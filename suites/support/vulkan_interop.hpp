#pragma once

#include "support/vulkan_rig.hpp"

#include <functional>
#include <vector>

namespace lsw::suites {
struct VulkanStep {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::uint64_t value = 0;
};

class VulkanInterop {
public:
    static constexpr VkExternalMemoryHandleTypeFlagBits kOpaque = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    static constexpr VkExternalMemoryHandleTypeFlagBits kOpaqueKmt = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;
    static constexpr VkExternalMemoryHandleTypeFlagBits kD3d11 = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    static constexpr VkExternalMemoryHandleTypeFlagBits kD3d11Kmt = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
    static constexpr VkExternalMemoryHandleTypeFlagBits kD3d12Heap = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT;
    static constexpr VkExternalMemoryHandleTypeFlagBits kD3d12Resource = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    static constexpr VkExternalSemaphoreHandleTypeFlagBits kOpaqueSemaphore = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    static constexpr VkExternalSemaphoreHandleTypeFlagBits kD3d12Fence = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

    [[nodiscard]] static VulkanRigOptions options();

    [[nodiscard]] static bool ready(const VulkanRig& rig, std::string& reason);

    [[nodiscard]] static VkExternalMemoryFeatureFlags buffer_features(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkBufferUsageFlags usage);

    [[nodiscard]] static VkExternalMemoryFeatureFlags image_features(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkFormat format, VkImageUsageFlags usage, VkResult* code = nullptr);

    [[nodiscard]] static VkExternalSemaphoreFeatureFlags semaphore_features(VulkanRig& rig, VkExternalSemaphoreHandleTypeFlagBits type, bool timeline);

    [[nodiscard]] static VkResult make_semaphore(VulkanRig& rig, VulkanOwned<VkSemaphore>& out, bool timeline, VkExternalSemaphoreHandleTypeFlags exportable);

    [[nodiscard]] static VkResult semaphore_handle(VulkanRig& rig, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits type, HANDLE& handle);

    [[nodiscard]] static VkResult import_semaphore(VulkanRig& rig, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits type, HANDLE handle);

    [[nodiscard]] static VkResult run(VulkanRig& rig, const std::function<void(VkCommandBuffer)>& record, const std::vector<VulkanStep>& waits,
                                      const std::vector<VulkanStep>& signals);
};

class VulkanSharedMemory {
public:
    VulkanSharedMemory() = default;
    VulkanSharedMemory(const VulkanSharedMemory&) = delete;
    VulkanSharedMemory& operator=(const VulkanSharedMemory&) = delete;
    ~VulkanSharedMemory() { destroy(); }

    [[nodiscard]] VkResult export_buffer(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, VkDeviceSize bytes, VkBufferUsageFlags usage);

    [[nodiscard]] VkResult import_buffer(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, HANDLE handle, VkDeviceSize bytes, VkBufferUsageFlags usage);

    [[nodiscard]] VkResult import_image(VulkanRig& rig, VkExternalMemoryHandleTypeFlagBits type, HANDLE handle, VkFormat format, std::uint32_t width,
                                        std::uint32_t height, VkImageUsageFlags usage);

    [[nodiscard]] VkResult handle(VkExternalMemoryHandleTypeFlagBits type, HANDLE& out) const;

    void destroy();

    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceSize allocation = 0;

private:
    [[nodiscard]] VkResult allocate(VkExternalMemoryHandleTypeFlagBits type, const VkMemoryRequirements& requirements, HANDLE import_handle, bool dedicated_buffer,
                                    bool dedicated_image);

    VulkanRig* rig_ = nullptr;
};
}  // namespace lsw::suites
