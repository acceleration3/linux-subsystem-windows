// Strong override for ``lsw::vkproto::handle_to_wire`` on dispatchable
// handle types. The codegen-emitted serializers route every
// ``serialize_<DispatchableHandle>`` call through this hook so handles
// nested in struct members or arrays (e.g. VkSubmitInfo.pCommandBuffers)
// get unwrapped from their ``LswWrappedHandle`` to the real driver
// handle before they hit the wire.
//
// Top-level dispatchable args go through ``unwrap_to_server_handle`` in
// the icd_entries wrapper directly; only nested ones reach this path.
//
// The default linkage in ``vk_handle_default.cpp`` is identity and
// marked weak; this file's strong definitions win when linked into the
// ICD ``.so``.

#include "lsw/icd/handle_wrap.hpp"
#include "vk_serialize.h"  // generated declarations

#include <cstdint>
#include <cstring>
#include <vulkan/vulkan_core.h>

namespace lsw::vkproto {

namespace {

template<typename Handle>
[[nodiscard]] std::uint64_t unwrap(Handle wrapped) noexcept {
    if (wrapped == VK_NULL_HANDLE) {
        return 0;
    }
    Handle server = ::lsw::icd::unwrap_to_server_handle(wrapped);
    std::uint64_t out = 0;
    static_assert(sizeof(server) == sizeof(out));
    std::memcpy(&out, &server, sizeof(out));
    return out;
}

}  // namespace

std::uint64_t handle_to_wire(VkInstance v) noexcept {
    return unwrap(v);
}

std::uint64_t handle_to_wire(VkPhysicalDevice v) noexcept {
    return unwrap(v);
}

std::uint64_t handle_to_wire(VkDevice v) noexcept {
    return unwrap(v);
}

std::uint64_t handle_to_wire(VkQueue v) noexcept {
    return unwrap(v);
}

std::uint64_t handle_to_wire(VkCommandBuffer v) noexcept {
    return unwrap(v);
}

}  // namespace lsw::vkproto
