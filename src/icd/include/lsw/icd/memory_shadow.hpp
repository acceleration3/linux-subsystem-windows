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
//
// THREADING. Every public method is safe to call from any thread, and
// that is not the usual Vulkan bargain. Vulkan externally synchronises
// ``vkMapMemory`` / ``vkUnmapMemory`` per ``VkDeviceMemory``, so a
// per-allocation lock would ordinarily be unnecessary — but
// ``flush_all_mapped`` runs on every queue submit and touches EVERY
// allocation, including one another thread is legitimately unmapping at
// that moment. We introduced that cross-allocation contact, so we owe
// the lock.
//
// The rule the implementation follows: bytes are copied into or out of a
// mapping only while ``mu_`` is held. A pointer into ``Allocation::mapped``
// must never outlive the critical section that produced it, because
// ``unmap_and_flush`` frees that buffer. Only the blocking transport
// round trip happens outside the lock.
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

    // Same, for an allocation the server IMPORTED out of the shared
    // region rather than allocating separately. ``arena_offset`` is
    // where it lives in the hostmem arena and ``mapped`` is this
    // process's view of those very bytes.
    //
    // The difference this makes is the whole of Phase G: ``map`` hands
    // the application a pointer into memory the GPU is already reading,
    // so every flush of this allocation — including the blunt
    // submit-time one — becomes nothing at all.
    void record_imported_allocation(std::uint64_t memory, VkDeviceSize size,
                                    std::uint64_t arena_offset, std::uint8_t* mapped);

    // Where an imported allocation lives in the arena, so the caller can
    // give the space back on free. ``RegionArena::kNoOffset`` when the
    // allocation is an ordinary copy-path one.
    [[nodiscard]] std::uint64_t imported_offset(std::uint64_t memory) const;

    // Called from the vkFreeMemory wrapper before forwarding. Frees
    // any local mapping buffer for the allocation.
    void release_allocation(std::uint64_t memory);

    // ``vkMapMemory``: allocate or reuse a host buffer covering
    // ``[offset, offset+size)``. Writes the buffer pointer to
    // ``*ppData``. Returns ``VK_ERROR_OUT_OF_HOST_MEMORY`` if the
    // allocation hasn't been recorded yet, or if size resolution fails.
    //
    // ``device`` is the server-side handle the allocation belongs to,
    // kept so a later blind flush knows where to send the bytes.
    [[nodiscard]] VkResult map(std::uint64_t device, std::uint64_t memory, VkDeviceSize offset,
                               VkDeviceSize size, void** ppData);

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

    // Ship every live mapping upstream, whether or not the app asked.
    //
    // This exists because HOST_COHERENT memory needs no flush by the
    // Vulkan rules: an app writes its uniforms straight through the
    // mapped pointer and submits, and a correct app never calls
    // ``vkFlushMappedMemoryRanges`` at all. Across a process boundary
    // there is no coherence to inherit, so without this the server
    // renders from whatever the buffer held at map time — a clear colour
    // and no geometry, which is exactly what vkcube showed.
    //
    // Called before every queue submit. Blunt: it re-sends whole
    // mappings rather than dirty pages, which is correct and slow. Dirty
    // tracking is the optimisation, not the fix.
    [[nodiscard]] VkResult flush_all_mapped(::lsw::protocol::Transport& t);

private:
    struct Allocation {
        VkDeviceSize size = 0;           // requested at vkAllocateMemory
        std::uint8_t* mapped = nullptr;  // non-null while mapped
        VkDeviceSize map_offset = 0;
        VkDeviceSize map_size = 0;
        std::uint64_t device = 0;        // server-side handle, recorded at map
        // Set when the server imported these bytes out of the shared
        // region. ``region_backed`` is what every flush path checks:
        // there is nothing to ship, because the server is already
        // reading the memory the application writes.
        bool region_backed = false;
        std::uint64_t arena_offset = 0;
        std::uint8_t* region_ptr = nullptr;  // our view of the same bytes
    };

    // Returns nullptr if the allocation is unknown.
    [[nodiscard]] Allocation* find_locked(std::uint64_t memory);

    mutable std::mutex mu_;
    std::unordered_map<std::uint64_t, Allocation> allocs_;
};

}  // namespace lsw::icd
