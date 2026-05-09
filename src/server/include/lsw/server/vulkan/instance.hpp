#pragma once

#include <vulkan/vulkan_core.h>

namespace lsw::server::vulkan {

// Owns a real ``VkInstance`` opened against the host's libvulkan loader,
// plus any per-instance bookkeeping the server needs. Phase 2 keeps this
// thin: the generated ``RealVulkanDispatch`` forwards directly to the
// libvulkan loader, so the only state we actually need is the handle
// itself and a destroy-on-RAII.
//
// As Phase 2.5+ adds surface intercept and the swapchain command set, this
// class will grow: cached physical-device list, enabled-extension set,
// debug-utils messenger, etc. The point of giving it its own file is so
// that growth has somewhere to land.
class Instance {
public:
    Instance() noexcept = default;

    explicit Instance(VkInstance handle) noexcept : handle_(handle) {}

    ~Instance() noexcept;

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&& other) noexcept;
    Instance& operator=(Instance&& other) noexcept;

    [[nodiscard]] VkInstance handle() const noexcept { return handle_; }

    [[nodiscard]] bool is_open() const noexcept { return handle_ != VK_NULL_HANDLE; }

    // Releases ownership without destroying.
    [[nodiscard]] VkInstance release() noexcept;

    // Destroys the underlying VkInstance via vkDestroyInstance and clears.
    void destroy() noexcept;

private:
    VkInstance handle_ = VK_NULL_HANDLE;
};

}  // namespace lsw::server::vulkan
