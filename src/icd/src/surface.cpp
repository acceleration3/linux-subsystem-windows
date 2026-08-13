#include "lsw/icd/surface.hpp"

#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/buffer_reader.hpp"
#include "lsw/protocol/frame.hpp"
#include "lsw/protocol/transport.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vulkan/vulkan_core.h>

// Forward decl so we don't pull xcb headers into the ICD .so.
struct xcb_connection_t;

namespace lsw::icd::surface {

namespace {

// The xcb create-info we read off — only ``window`` matters because
// the server opens its own xcb connection. Defining this locally keeps
// the ICD off xcb headers.
struct VkXcbSurfaceCreateInfoKHR_LSW {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    xcb_connection_t* connection;
    std::uint32_t window;
};

// The Win32 create-info, same treatment: only the shape matters, and
// declaring it here keeps windows.h out of the ICD. ``HINSTANCE`` and
// ``HWND`` are both pointers.
struct VkWin32SurfaceCreateInfoKHR_LSW {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* hinstance;
    void* hwnd;
};

template<typename Handle>
[[nodiscard]] std::uint64_t pack_u64(Handle h) noexcept {
    std::uint64_t out = 0;
    static_assert(sizeof(h) == sizeof(out));
    std::memcpy(&out, &h, sizeof(out));
    return out;
}

[[nodiscard]] VkSurfaceKHR unpack_surface(std::uint64_t raw) noexcept {
    VkSurfaceKHR out{};
    static_assert(sizeof(out) == sizeof(raw));
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

// The host creates the window, so the client has to say how big it
// should be. It cannot ask the window it was handed — reading X
// geometry would put xcb back in the ICD, and on a Windows guest there
// is no host window to measure in the first place.
//
// So: a default, overridable per-run. The app then sizes its swapchain
// from ``vkGetPhysicalDeviceSurfaceCapabilitiesKHR``, which the server
// answers from the real surface, so whatever is chosen here is what the
// app actually renders at.
constexpr std::uint32_t kDefaultWindowWidth = 1024;
constexpr std::uint32_t kDefaultWindowHeight = 768;

struct WindowSize {
    std::uint32_t width = kDefaultWindowWidth;
    std::uint32_t height = kDefaultWindowHeight;
};

[[nodiscard]] WindowSize desired_window_size() {
    WindowSize out;
    const char* env = std::getenv("LSW_WINDOW_SIZE");  // "1280x720"
    if (env == nullptr || *env == '\0') {
        return out;
    }
    char* rest = nullptr;
    const unsigned long w = std::strtoul(env, &rest, 10);
    if (rest == nullptr || (*rest != 'x' && *rest != 'X') || w == 0) {
        return out;
    }
    const unsigned long h = std::strtoul(rest + 1, nullptr, 10);
    if (h == 0) {
        return out;
    }
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    return out;
}

// Ask the server for a window and a surface on it. Shared by every
// platform's surface entry point: what the client's own windowing
// system calls a window is irrelevant here — the pixels land in a HOST
// window either way, which is the entire point of the proxy.
[[nodiscard]] VkResult create_host_surface(VkInstance instance, VkSurfaceKHR* pSurface) {
    auto* conn = ::lsw::icd::connection_of(instance);
    if (conn == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const WindowSize size = desired_window_size();
    const std::string title = conn->app_name();

    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(pack_u64(::lsw::icd::unwrap_to_server_handle(instance)));
    payload.write_pod<std::uint32_t>(size.width);
    payload.write_pod<std::uint32_t>(size.height);
    payload.write_pod<std::uint32_t>(static_cast<std::uint32_t>(title.size()));
    payload.write(title.data(), title.size());

    ::lsw::protocol::Buffer reply;
    if (conn->transport().send_sync(::lsw::protocol::kCmdCreateHostSurface, payload.bytes(),
                                    &reply) != ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t rc = 0;
    std::uint64_t synthetic_id = 0;
    if (!r.read_pod(rc) || !r.read_pod(synthetic_id)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (rc != VK_SUCCESS) {
        return static_cast<VkResult>(rc);
    }
    *pSurface = unpack_surface(synthetic_id);
    return VK_SUCCESS;
}

// ---- Entry implementations ------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL CreateXcbSurfaceKHR(VkInstance instance,
                                                   const VkXcbSurfaceCreateInfoKHR_LSW* pCreateInfo,
                                                   const VkAllocationCallbacks* /*pAllocator*/,
                                                   VkSurfaceKHR* pSurface) {
    if (pSurface == nullptr || pCreateInfo == nullptr || instance == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // ``pCreateInfo->window`` is deliberately ignored. Presenting into a
    // window owned by another process succeeds at every call and paints
    // nothing on screen, and a guest application's window does not exist
    // on the host display at all. The server makes its own window.
    return create_host_surface(instance, pSurface);
}

VKAPI_ATTR VkResult VKAPI_CALL
CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR_LSW* pCreateInfo,
                      const VkAllocationCallbacks* /*pAllocator*/, VkSurfaceKHR* pSurface) {
    if (pSurface == nullptr || pCreateInfo == nullptr || instance == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // The HWND is ignored for exactly the reason the xcb window is: the
    // pixels land in a window the HOST owns. In the guest that is not a
    // simplification but the only option — the application's HWND lives
    // inside the VM, where nothing can present it to a Linux desktop.
    //
    // This is the whole of the Win32 surface support, and it is short
    // because everything underneath it was already platform-agnostic.
    return create_host_surface(instance, pSurface);
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                             const VkAllocationCallbacks* /*pAllocator*/) {
    if (instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
        return;
    }
    auto* conn = ::lsw::icd::connection_of(instance);
    if (conn == nullptr) {
        return;
    }
    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(pack_u64(::lsw::icd::unwrap_to_server_handle(instance)));
    payload.write_pod<std::uint64_t>(pack_u64(surface));
    ::lsw::protocol::Buffer reply;
    (void)conn->transport().send_sync(::lsw::protocol::kCmdDestroySurface, payload.bytes(), &reply);
}

// Every queue family on every physical device is reported as
// supporting our xcb-backed surface. The wire-routed
// vkGetPhysicalDeviceSurfaceSupportKHR (which DOES go through
// codegen + TranslatingDispatch) gives a more precise answer once a
// real VkSurfaceKHR exists.
VKAPI_ATTR VkBool32 VKAPI_CALL GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice /*phys*/, uint32_t /*queueFamilyIndex*/, xcb_connection_t* /*connection*/,
    uint32_t /*visual_id*/) {
    return VK_TRUE;
}

// Not ``constexpr`` because reinterpret_cast to function-pointer is not
// a constant expression. Plain runtime-initialised const is fine.
VKAPI_ATTR VkBool32 VKAPI_CALL GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice /*phys*/, uint32_t /*queueFamilyIndex*/) {
    return VK_TRUE;
}

// Both platforms' entries are registered on both platforms, which costs
// nothing: a Linux application never asks for the Win32 entry and a
// guest application never asks for the xcb one. Compiling one set out
// would mean the ICD's source differed by target for no gain, and the
// rule this project runs on is that it does not.
const std::array<Entry, 5> kEntries = {{
    {"vkCreateXcbSurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(&CreateXcbSurfaceKHR)},
    {"vkCreateWin32SurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(&CreateWin32SurfaceKHR)},
    {"vkDestroySurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(&DestroySurfaceKHR)},
    {"vkGetPhysicalDeviceXcbPresentationSupportKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceXcbPresentationSupportKHR)},
    {"vkGetPhysicalDeviceWin32PresentationSupportKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceWin32PresentationSupportKHR)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace lsw::icd::surface
