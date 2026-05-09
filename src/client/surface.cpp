#include "lsw/icd/surface.hpp"

#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"
#include "lsw/protocol/transport.hpp"

#include <array>
#include <cstdint>
#include <cstring>
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

// ---- Entry implementations ------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL CreateXcbSurfaceKHR(VkInstance instance,
                                                   const VkXcbSurfaceCreateInfoKHR_LSW* pCreateInfo,
                                                   const VkAllocationCallbacks* /*pAllocator*/,
                                                   VkSurfaceKHR* pSurface) {
    if (pSurface == nullptr || pCreateInfo == nullptr || instance == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto* conn = ::lsw::icd::connection_of(instance);
    if (conn == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(pack_u64(::lsw::icd::unwrap_to_server_handle(instance)));
    payload.write_pod<std::uint32_t>(pCreateInfo->window);

    ::lsw::protocol::Buffer reply;
    if (conn->transport().send_sync(::lsw::protocol::kCmdCreateXcbSurface, payload.bytes(),
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
const std::array<Entry, 3> kEntries = {{
    {"vkCreateXcbSurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(&CreateXcbSurfaceKHR)},
    {"vkDestroySurfaceKHR", reinterpret_cast<PFN_vkVoidFunction>(&DestroySurfaceKHR)},
    {"vkGetPhysicalDeviceXcbPresentationSupportKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceXcbPresentationSupportKHR)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace lsw::icd::surface
