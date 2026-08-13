#pragma once

#include <vulkan/vulkan_core.h>

namespace lsw::render::vulkan {

// Owns a real ``VkDevice``. As with ``Instance``, kept deliberately thin
// in Phase 2; future iterations grow it to track per-device queue
// families, enabled extensions, the device's pipeline cache path, etc.
class Device {
public:
    Device() noexcept = default;

    explicit Device(VkDevice handle) noexcept : handle_(handle) {}

    ~Device() noexcept;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;

    [[nodiscard]] VkDevice handle() const noexcept { return handle_; }

    [[nodiscard]] bool is_open() const noexcept { return handle_ != VK_NULL_HANDLE; }

    [[nodiscard]] VkDevice release() noexcept;
    void destroy() noexcept;

private:
    VkDevice handle_ = VK_NULL_HANDLE;
};

}  // namespace lsw::render::vulkan
