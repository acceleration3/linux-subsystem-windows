#pragma once

#include "lsw/icd/command_recorder.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vulkan/vk_icd.h>

namespace lsw::icd {

class Connection;

// The Vulkan loader requires every dispatchable handle returned by an ICD
// to point at memory whose first ``uintptr_t`` equals ``ICD_LOADER_MAGIC``.
// We allocate one of these per dispatchable handle (VkInstance,
// VkPhysicalDevice, VkDevice, VkQueue, VkCommandBuffer); the loader
// validates pointer provenance via ``loader_magic`` and we keep the
// authoritative server handle plus a back-pointer to the per-instance
// ``Connection`` whose transport this handle belongs to.
struct LswWrappedHandle {
    uintptr_t loader_magic;  // must come first; see vk_icd.h
    std::uint64_t server_handle;
    Connection* connection;  // non-owning; per-instance
    // Non-null only for VkCommandBuffer. The batch of recorded vkCmd*
    // calls lives here, hanging off the handle itself rather than in a
    // process-wide map, because a map would need a lock and the lock
    // would sit in the hot path of every single recorded command — the
    // exact contention batching exists to remove.
    CommandRecorder* recorder;
};

template<typename Handle>
inline Handle wrap_server_handle(Handle server, Connection* connection) noexcept {
    static_assert(sizeof(Handle) == sizeof(void*),
                  "wrap_server_handle is only for dispatchable handles");
    if (server == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    auto* w = new LswWrappedHandle{};
    w->loader_magic = ICD_LOADER_MAGIC;
    std::memcpy(&w->server_handle, &server, sizeof(server));
    w->connection = connection;
    if constexpr (std::is_same_v<Handle, VkCommandBuffer>) {
        w->recorder = new CommandRecorder{};
    } else {
        w->recorder = nullptr;
    }
    Handle out{};
    void* raw = w;
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

// Where a recorded ``vkCmd*`` writes its bytes: the command buffer's own
// batch. Named by the generated wrappers, defined in cmdbuf.cpp.
//
// Falls back to the connection's transport if the handle somehow has no
// recorder, so a missing batch degrades to the old one-message-per-call
// behaviour rather than to a null dereference.
[[nodiscard]] ::lsw::protocol::Transport& record_target(VkCommandBuffer wrapped) noexcept;

// The recorder belonging to ``wrapped``, or nullptr if it has none.
inline CommandRecorder* recorder_of(VkCommandBuffer wrapped) noexcept {
    if (wrapped == VK_NULL_HANDLE) {
        return nullptr;
    }
    void* raw = nullptr;
    std::memcpy(&raw, &wrapped, sizeof(raw));
    return static_cast<const LswWrappedHandle*>(raw)->recorder;
}

template<typename Handle>
inline Handle unwrap_to_server_handle(Handle wrapped) noexcept {
    if (wrapped == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    void* raw = nullptr;
    std::memcpy(&raw, &wrapped, sizeof(raw));
    const auto* w = static_cast<const LswWrappedHandle*>(raw);
    Handle out{};
    std::memcpy(&out, &w->server_handle, sizeof(out));
    return out;
}

template<typename Handle>
inline Connection* connection_of(Handle wrapped) noexcept {
    if (wrapped == VK_NULL_HANDLE) {
        return nullptr;
    }
    void* raw = nullptr;
    std::memcpy(&raw, &wrapped, sizeof(raw));
    return static_cast<const LswWrappedHandle*>(raw)->connection;
}

template<typename Handle>
inline void destroy_wrapper(Handle wrapped) noexcept {
    if (wrapped == VK_NULL_HANDLE) {
        return;
    }
    void* raw = nullptr;
    std::memcpy(&raw, &wrapped, sizeof(raw));
    auto* w = static_cast<LswWrappedHandle*>(raw);
    delete w->recorder;
    delete w;
}

}  // namespace lsw::icd
