#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

// Forward-declare xcb_connection_t so this header doesn't pull xcb in.
struct xcb_connection_t;

namespace lsw::render {

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
    //
    // Only usable when the client shares this X display: presenting into
    // a window owned by another process does not reach the screen (the
    // calls all succeed and nothing paints). Kept for the loopback
    // harness; ``create_host_window`` is the path that works.
    [[nodiscard]] VkResult create_from_xcb(VkInstance instance, std::uint32_t window,
                                           std::uint64_t* out_id);

    // Create a window on the HOST display, map it, and return a surface
    // for it. The window belongs to this process, so presenting to it
    // actually reaches the screen — and it is what the guest needs,
    // since a guest application has no window here at all.
    [[nodiscard]] VkResult create_host_window(VkInstance instance, std::uint32_t width,
                                              std::uint32_t height, const std::string& title,
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
    // Mint an id for ``real`` under the lock. ``owned_window`` is the
    // xcb window to destroy along with the surface, or 0 when the window
    // belongs to someone else.
    [[nodiscard]] std::uint64_t record(VkSurfaceKHR real, std::uint32_t owned_window);

    struct Entry {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::uint32_t owned_window = 0;  // 0 = not ours to destroy
    };

    mutable std::mutex mu_;
    xcb_connection_t* xcb_conn_ = nullptr;
    std::unordered_map<std::uint64_t, Entry> map_;
    std::uint64_t next_id_ = 1;
};

}  // namespace lsw::render
