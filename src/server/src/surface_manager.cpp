#include "lsw/server/surface_manager.hpp"

#include <cstring>
#include <xcb/xcb.h>

#define VK_USE_PLATFORM_XCB_KHR
#include "lsw/log.hpp"

#include <vulkan/vulkan_xcb.h>

namespace lsw::server {

SurfaceManager::~SurfaceManager() {
    // Surfaces are destroyed via destroy() while the VkInstance still
    // exists. The xcb connection lives until process exit.
    if (xcb_conn_ != nullptr) {
        xcb_disconnect(xcb_conn_);
        xcb_conn_ = nullptr;
    }
}

int SurfaceManager::ensure_xcb() {
    std::lock_guard lk(mu_);
    if (xcb_conn_ != nullptr) {
        return 0;
    }
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    if (c == nullptr || xcb_connection_has_error(c) != 0) {
        if (c != nullptr) {
            xcb_disconnect(c);
        }
        LSW_ERROR("xcb_connect failed (no $DISPLAY?)");
        return -1;
    }
    xcb_conn_ = c;
    LSW_DEBUG("xcb connection opened");
    return 0;
}

VkResult SurfaceManager::create_from_xcb(VkInstance instance, std::uint32_t window,
                                         std::uint64_t* out_id) {
    if (ensure_xcb() != 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto pfn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
        ::vkGetInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR"));
    if (pfn == nullptr) {
        LSW_ERROR("host driver missing VK_KHR_xcb_surface");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    VkXcbSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    info.connection = xcb_conn_;
    info.window = static_cast<xcb_window_t>(window);

    VkSurfaceKHR real = VK_NULL_HANDLE;
    const VkResult rc = pfn(instance, &info, nullptr, &real);
    if (rc != VK_SUCCESS) {
        LSW_WARN("vkCreateXcbSurfaceKHR(window=0x{:x}) -> {}", window, static_cast<int>(rc));
        return rc;
    }

    std::lock_guard lk(mu_);
    const std::uint64_t id = next_id_++;
    map_.emplace(id, real);
    if (out_id != nullptr) {
        *out_id = id;
    }
    LSW_DEBUG("SurfaceManager: minted synthetic id={} -> real surface={} (xcb window=0x{:x})", id,
              static_cast<const void*>(real), window);
    return VK_SUCCESS;
}

VkSurfaceKHR SurfaceManager::resolve(std::uint64_t synthetic_id) const noexcept {
    std::lock_guard lk(mu_);
    auto it = map_.find(synthetic_id);
    return it == map_.end() ? VK_NULL_HANDLE : it->second;
}

void SurfaceManager::destroy(VkInstance instance, std::uint64_t synthetic_id) {
    VkSurfaceKHR real = VK_NULL_HANDLE;
    {
        std::lock_guard lk(mu_);
        auto it = map_.find(synthetic_id);
        if (it == map_.end()) {
            return;
        }
        real = it->second;
        map_.erase(it);
    }
    auto pfn = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        ::vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (pfn != nullptr && real != VK_NULL_HANDLE) {
        pfn(instance, real, nullptr);
    }
}

}  // namespace lsw::server
