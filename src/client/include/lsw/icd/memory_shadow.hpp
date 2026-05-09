#pragma once

#include "lsw/protocol/transport.hpp"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace lsw::icd {

// Per-VkInstance host-visible memory mirror. See docs/architecture.md
// (Memory: the hard part) for the full design.
//
// Phase 1 — no shared memory. ``vkMapMemory`` allocates a guest-side
// buffer of the requested size and returns a pointer to it; the app
// reads/writes that buffer freely. On ``vkUnmapMemory`` (and on
// ``vkFlushMappedMemoryRanges``) we ship dirty bytes upstream via
// ``kCmdMemoryWrite`` and the server replays them onto the real
// ``VkDeviceMemory``. ``vkInvalidateMappedMemoryRanges`` pulls bytes
// the other direction via ``kCmdMemoryRead``.
//
// Tracking allocation size lets us resolve ``VK_WHOLE_SIZE`` at
// flush time without an extra round trip; it's recorded by the
// ``vkAllocateMemory`` wrapper before any client code can call
// ``vkMapMemory``.
class MemoryShadow {
public:
    MemoryShadow() = default;
    ~MemoryShadow();

    MemoryShadow(const MemoryShadow&) = delete;
    MemoryShadow& operator=(const MemoryShadow&) = delete;
    MemoryShadow(MemoryShadow&&) = delete;
    MemoryShadow& operator=(MemoryShadow&&) = delete;

    // Called from the vkAllocateMemory wrapper after the server-side
    // call succeeds. ``memory`` is the raw 64-bit handle the server
    // returned; ``size`` is the requested allocation size.
    void record_allocation(std::uint64_t memory, VkDeviceSize size);

    // Called from the vkFreeMemory wrapper before forwarding. Frees
    // any local mapping buffer for the allocation.
    void release_allocation(std::uint64_t memory);

    // ``vkMapMemory``: allocate or reuse a host buffer covering
    // ``[offset, offset+size)``. Writes the buffer pointer to
    // ``*ppData``. Returns ``VK_ERROR_OUT_OF_HOST_MEMORY`` if the
    // allocation hasn't been recorded yet, or if size resolution fails.
    [[nodiscard]] VkResult map(std::uint64_t memory, VkDeviceSize offset, VkDeviceSize size,
                               void** ppData);

    // ``vkUnmapMemory``: ship the entire mapping to the server, then
    // free the local buffer. ``device`` is the server-side dispatchable
    // device handle (raw 64-bit) so the server can call vkMapMemory
    // on the right VkDevice.
    [[nodiscard]] VkResult unmap_and_flush(::lsw::protocol::Transport& t, std::uint64_t device,
                                           std::uint64_t memory);

    // ``vkFlushMappedMemoryRanges``: ship a sub-range without unmapping.
    [[nodiscard]] VkResult flush_range(::lsw::protocol::Transport& t, std::uint64_t device,
                                       std::uint64_t memory, VkDeviceSize offset,
                                       VkDeviceSize size);

    // ``vkInvalidateMappedMemoryRanges``: pull a sub-range from the
    // server into the local buffer.
    [[nodiscard]] VkResult invalidate_range(::lsw::protocol::Transport& t, std::uint64_t device,
                                            std::uint64_t memory, VkDeviceSize offset,
                                            VkDeviceSize size);

private:
    struct Allocation {
        VkDeviceSize size = 0;           // requested at vkAllocateMemory
        std::uint8_t* mapped = nullptr;  // non-null while mapped
        VkDeviceSize map_offset = 0;
        VkDeviceSize map_size = 0;
    };

    // Returns nullptr if the allocation is unknown.
    [[nodiscard]] Allocation* find_locked(std::uint64_t memory);

    mutable std::mutex mu_;
    std::unordered_map<std::uint64_t, Allocation> allocs_;
};

}  // namespace lsw::icd
