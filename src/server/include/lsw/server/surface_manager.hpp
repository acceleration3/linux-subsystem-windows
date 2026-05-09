#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

// Forward-declare xcb_connection_t so this header doesn't pull xcb in.
struct xcb_connection_t;

namespace lsw::server {

// Process-global registry that turns guest-side synthetic surface IDs
// into real ``VkSurfaceKHR`` handles owned by the host Vulkan driver.
//
// The client ICD's ``vkCreateXcbSurfaceKHR`` no longer makes up a local
// fake handle — it sends a ``kCmdCreateXcbSurface`` frame carrying just
// the X11 window id. The server (this class) opens its own xcb
// connection to the same X display, calls ``vkCreateXcbSurfaceKHR`` on
// the host driver, and hands a fresh synthetic id back. Subsequent
// surface queries (``vkGetPhysicalDeviceSurface*KHR``,
// ``vkCreateSwapchainKHR``) translate the synthetic id through this
// table before the call reaches the real driver.
//
// A single process-wide instance is fine for now — serve_one is
// single-connection — and a mutex keeps it future-proof against the
// multi-connection refactor in Phase 4.
class SurfaceManager {
public:
    SurfaceManager() = default;
    ~SurfaceManager();

    SurfaceManager(const SurfaceManager&) = delete;
    SurfaceManager& operator=(const SurfaceManager&) = delete;
    SurfaceManager(SurfaceManager&&) = delete;
    SurfaceManager& operator=(SurfaceManager&&) = delete;

    // Lazy-connect to ``$DISPLAY``. Returns 0 on success, -1 on failure.
    [[nodiscard]] int ensure_xcb();

    // Create a ``VkSurfaceKHR`` from an existing X11 window id, mint a
    // synthetic id for the wire, and remember the mapping. Returns
    // ``VK_SUCCESS`` and writes ``*out_id`` on success.
    [[nodiscard]] VkResult create_from_xcb(VkInstance instance, std::uint32_t window,
                                           std::uint64_t* out_id);

    // Translate a synthetic id back into the real driver handle. Returns
    // ``VK_NULL_HANDLE`` for unknown ids — the codegen forwards that
    // through to the driver, which will emit a clean validation error
    // rather than the SIGSEGV we'd get from a bogus pointer.
    [[nodiscard]] VkSurfaceKHR resolve(std::uint64_t synthetic_id) const noexcept;

    // Tear down a surface (calls ``vkDestroySurfaceKHR`` on the driver).
    // Safe with unknown ids.
    void destroy(VkInstance instance, std::uint64_t synthetic_id);

private:
    mutable std::mutex mu_;
    xcb_connection_t* xcb_conn_ = nullptr;
    std::unordered_map<std::uint64_t, VkSurfaceKHR> map_;
    std::uint64_t next_id_ = 1;
};

}  // namespace lsw::server
