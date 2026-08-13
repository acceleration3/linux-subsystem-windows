#include "lsw/render/surface_manager.hpp"

#include <cstring>
#include <utility>
#include <xcb/xcb.h>

#define VK_USE_PLATFORM_XCB_KHR
#include "lsw/base/log.hpp"

#include <vulkan/vulkan_xcb.h>

namespace lsw::render {

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

    const std::uint64_t id = record(real, /*owned_window=*/0);
    if (out_id != nullptr) {
        *out_id = id;
    }
    LSW_DEBUG("SurfaceManager: minted synthetic id={} -> real surface={} (foreign xcb window=0x{:x})",
              id, static_cast<const void*>(real), window);
    return VK_SUCCESS;
}

VkResult SurfaceManager::create_host_window(VkInstance instance, std::uint32_t width,
                                            std::uint32_t height, const std::string& title,
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

    xcb_connection_t* c = nullptr;
    {
        std::lock_guard lk(mu_);
        c = xcb_conn_;
    }
    const xcb_setup_t* setup = xcb_get_setup(c);
    xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;
    if (screen == nullptr) {
        LSW_ERROR("xcb: display has no screen");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const xcb_window_t window = xcb_generate_id(c);
    const std::uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const std::uint32_t values[2] = {screen->black_pixel,
                                     XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(c, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0,
                      static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                      /*border=*/0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask,
                      values);
    if (!title.empty()) {
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                            static_cast<std::uint32_t>(title.size()), title.data());
        // WM_CLASS is two NUL-terminated strings; give the window its own
        // instance/class so a desktop can group and icon it separately.
        const std::string wm_class = title + '\0' + "lsw-render" + '\0';
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                            static_cast<std::uint32_t>(wm_class.size()), wm_class.data());
    }
    xcb_map_window(c, window);
    xcb_flush(c);

    VkXcbSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    info.connection = c;
    info.window = window;

    VkSurfaceKHR real = VK_NULL_HANDLE;
    const VkResult rc = pfn(instance, &info, nullptr, &real);
    if (rc != VK_SUCCESS) {
        LSW_WARN("vkCreateXcbSurfaceKHR(host window 0x{:x}) -> {}", window, static_cast<int>(rc));
        xcb_destroy_window(c, window);
        xcb_flush(c);
        return rc;
    }

    const std::uint64_t id = record(real, window);
    if (out_id != nullptr) {
        *out_id = id;
    }
    LSW_INFO("SurfaceManager: host window 0x{:x} ({}x{}) for \"{}\" -> synthetic id={}", window,
             width, height, title, id);
    return VK_SUCCESS;
}

std::uint64_t SurfaceManager::record(VkSurfaceKHR real, std::uint32_t owned_window) {
    std::lock_guard lk(mu_);
    const std::uint64_t id = next_id_++;
    map_.emplace(id, Entry{real, owned_window});
    return id;
}

VkSurfaceKHR SurfaceManager::resolve(std::uint64_t synthetic_id) const noexcept {
    std::lock_guard lk(mu_);
    auto it = map_.find(synthetic_id);
    return it == map_.end() ? VK_NULL_HANDLE : it->second.surface;
}

void SurfaceManager::destroy(VkInstance instance, std::uint64_t synthetic_id) {
    Entry entry;
    xcb_connection_t* c = nullptr;
    {
        std::lock_guard lk(mu_);
        auto it = map_.find(synthetic_id);
        if (it == map_.end()) {
            return;
        }
        entry = it->second;
        map_.erase(it);
        c = xcb_conn_;
    }
    auto pfn = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        ::vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (pfn != nullptr && entry.surface != VK_NULL_HANDLE) {
        pfn(instance, entry.surface, nullptr);
    }
    // Only windows we created are ours to take down.
    if (entry.owned_window != 0 && c != nullptr) {
        xcb_destroy_window(c, entry.owned_window);
        xcb_flush(c);
    }
}

}  // namespace lsw::render
