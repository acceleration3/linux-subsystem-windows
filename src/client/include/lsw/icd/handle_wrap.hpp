#pragma once

#include <cstdint>
#include <cstring>
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
    Handle out{};
    void* raw = w;
    std::memcpy(&out, &raw, sizeof(out));
    return out;
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
    delete static_cast<LswWrappedHandle*>(raw);
}

}  // namespace lsw::icd
