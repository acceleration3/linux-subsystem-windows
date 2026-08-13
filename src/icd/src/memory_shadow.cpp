#include "lsw/icd/memory_shadow.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/buffer_reader.hpp"
#include "lsw/protocol/frame.hpp"
#include "lsw/protocol/region_arena.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace lsw::icd {

namespace {

[[nodiscard]] VkDeviceSize resolve_size(VkDeviceSize requested, VkDeviceSize alloc_size,
                                        VkDeviceSize offset) noexcept {
    if (requested == VK_WHOLE_SIZE) {
        return alloc_size > offset ? alloc_size - offset : 0;
    }
    return requested;
}

}  // namespace

MemoryShadow::~MemoryShadow() {
    // Free any still-mapped guest buffers; the spec calls
    // post-vkDestroyDevice teardown undefined, but we don't want to
    // leak in test fixtures.
    std::lock_guard lk(mu_);
    for (auto& [_, a] : allocs_) {
        if (!a.region_backed) {
            std::free(a.mapped);
        }
    }
    allocs_.clear();
}

MemoryShadow::Allocation* MemoryShadow::find_locked(std::uint64_t memory) {
    auto it = allocs_.find(memory);
    return it == allocs_.end() ? nullptr : &it->second;
}

void MemoryShadow::record_allocation(std::uint64_t memory, VkDeviceSize size) {
    std::lock_guard lk(mu_);
    Allocation a{};
    a.size = size;
    allocs_[memory] = a;
}

void MemoryShadow::record_imported_allocation(std::uint64_t memory, VkDeviceSize size,
                                              std::uint64_t arena_offset,
                                              std::uint8_t* mapped) {
    std::lock_guard lk(mu_);
    Allocation a{};
    a.size = size;
    a.region_backed = true;
    a.arena_offset = arena_offset;
    a.region_ptr = mapped;
    allocs_[memory] = a;
}

std::uint64_t MemoryShadow::imported_offset(std::uint64_t memory) const {
    std::lock_guard lk(mu_);
    const auto it = allocs_.find(memory);
    if (it == allocs_.end() || !it->second.region_backed) {
        return ::lsw::protocol::RegionArena::kNoOffset;
    }
    return it->second.arena_offset;
}

void MemoryShadow::release_allocation(std::uint64_t memory) {
    std::lock_guard lk(mu_);
    auto it = allocs_.find(memory);
    if (it == allocs_.end()) {
        return;
    }
    if (!it->second.region_backed) {
        // Region-backed bytes belong to the arena, not to malloc; the
        // caller gives the offset back with imported_offset().
        std::free(it->second.mapped);
    }
    allocs_.erase(it);
}

VkResult MemoryShadow::map(std::uint64_t device, std::uint64_t memory, VkDeviceSize offset,
                           VkDeviceSize size, void** ppData) {
    if (ppData == nullptr) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    std::lock_guard lk(mu_);
    Allocation* a = find_locked(memory);
    if (a == nullptr) {
        // Either vkAllocateMemory wasn't routed through us, or the app
        // is mapping a freed handle. Fail loudly — silent fallback
        // would mask much harder bugs downstream.
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (a->region_backed) {
        // The point of the whole phase: hand the application a pointer
        // into memory the server has already imported as VkDeviceMemory.
        // There is no shadow buffer, nothing to prefetch, and — the part
        // that actually costs time today — nothing to flush at submit.
        const VkDeviceSize map_size = resolve_size(size, a->size, offset);
        if (map_size == 0 || a->region_ptr == nullptr) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        a->mapped = a->region_ptr;
        a->map_offset = offset;
        a->map_size = map_size;
        a->device = device;
        *ppData = a->region_ptr + offset;
        return VK_SUCCESS;
    }
    if (a->mapped != nullptr) {
        // Vulkan disallows mapping an already-mapped allocation, but
        // some apps double-map; reuse the existing buffer.
        *ppData = a->mapped + (offset - a->map_offset);
        return VK_SUCCESS;
    }
    const VkDeviceSize map_size = resolve_size(size, a->size, offset);
    if (map_size == 0) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    auto* buf = static_cast<std::uint8_t*>(std::malloc(map_size));
    if (buf == nullptr) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    // Phase 1 doesn't pre-fetch existing contents from the server —
    // apps that map-and-read uninitialised memory get zeros, which is
    // what most allocation paths produce anyway.
    std::memset(buf, 0, map_size);
    a->mapped = buf;
    a->map_offset = offset;
    a->map_size = map_size;
    a->device = device;
    *ppData = buf;
    return VK_SUCCESS;
}

VkResult MemoryShadow::unmap_and_flush(::lsw::protocol::Transport& t, std::uint64_t device,
                                       std::uint64_t memory) {
    std::uint8_t* buf = nullptr;
    VkDeviceSize off = 0;
    VkDeviceSize size = 0;
    {
        std::lock_guard lk(mu_);
        Allocation* a = find_locked(memory);
        if (a == nullptr || a->mapped == nullptr) {
            return VK_SUCCESS;
        }
        if (a->region_backed) {
            // Nothing to send: the server has been reading these exact
            // bytes all along. Just forget the mapping.
            a->mapped = nullptr;
            a->map_offset = 0;
            a->map_size = 0;
            return VK_SUCCESS;
        }
        buf = a->mapped;
        off = a->map_offset;
        size = a->map_size;
        a->mapped = nullptr;
        a->map_offset = 0;
        a->map_size = 0;
    }

    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(device);
    payload.write_pod<std::uint64_t>(memory);
    payload.write_pod<std::uint64_t>(off);
    payload.write_pod<std::uint64_t>(size);
    payload.write(buf, static_cast<std::size_t>(size));
    std::free(buf);

    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::protocol::kCmdMemoryWrite, payload.bytes(), &reply) !=
        ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t rc = 0;
    if (!r.read_pod(rc)) {
        return VK_ERROR_DEVICE_LOST;
    }
    return static_cast<VkResult>(rc);
}

VkResult MemoryShadow::flush_range(::lsw::protocol::Transport& t, std::uint64_t device,
                                   std::uint64_t memory, VkDeviceSize offset, VkDeviceSize size) {
    ::lsw::protocol::Buffer payload;
    {
        // The bytes are copied INSIDE the lock, not just the pointer to
        // them. Vulkan lets one thread unmap allocation Y while another
        // submits, and ``flush_all_mapped`` turns every submit into a
        // flush of Y — so releasing the lock with only ``a->mapped`` in
        // hand leaves a window in which ``unmap_and_flush`` frees the
        // buffer we are about to read. Only the round trip has to be
        // outside the lock, and it still is.
        std::lock_guard lk(mu_);
        Allocation* a = find_locked(memory);
        if (a == nullptr || a->mapped == nullptr) {
            return VK_SUCCESS;
        }
        if (a->region_backed) {
            // A flush of shared memory is a no-op by construction —
            // this is what Phase G buys, and it is bought here.
            return VK_SUCCESS;
        }
        const VkDeviceSize actual_size =
            resolve_size(size, a->map_size, offset - a->map_offset);
        if (actual_size == 0) {
            return VK_SUCCESS;
        }
        payload.write_pod<std::uint64_t>(device);
        payload.write_pod<std::uint64_t>(memory);
        payload.write_pod<std::uint64_t>(offset);
        payload.write_pod<std::uint64_t>(actual_size);
        payload.write(a->mapped + (offset - a->map_offset),
                      static_cast<std::size_t>(actual_size));
    }

    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::protocol::kCmdMemoryWrite, payload.bytes(), &reply) !=
        ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t rc = 0;
    if (!r.read_pod(rc)) {
        return VK_ERROR_DEVICE_LOST;
    }
    return static_cast<VkResult>(rc);
}

VkResult MemoryShadow::flush_all_mapped(::lsw::protocol::Transport& t) {
    // Snapshot under the lock, ship outside it: the transport call is a
    // blocking round trip and must not hold up an unrelated map/unmap.
    struct Pending {
        std::uint64_t device;
        std::uint64_t memory;
        VkDeviceSize offset;
        VkDeviceSize size;
    };
    std::vector<Pending> pending;
    {
        std::lock_guard lk(mu_);
        pending.reserve(allocs_.size());
        for (const auto& [memory, a] : allocs_) {
            // Region-backed allocations are skipped outright. On a run
            // where every mapping is imported this loop finds nothing to
            // do, which is the submit-time copy disappearing.
            if (!a.region_backed && a.mapped != nullptr && a.map_size > 0) {
                pending.push_back(Pending{a.device, memory, a.map_offset, a.map_size});
            }
        }
    }
    for (const auto& p : pending) {
        const VkResult rc = flush_range(t, p.device, p.memory, p.offset, p.size);
        if (rc != VK_SUCCESS) {
            return rc;
        }
    }
    return VK_SUCCESS;
}

VkResult MemoryShadow::invalidate_range(::lsw::protocol::Transport& t, std::uint64_t device,
                                        std::uint64_t memory, VkDeviceSize offset,
                                        VkDeviceSize size) {
    VkDeviceSize actual_size = 0;
    {
        // Only to decide the size and that there is anything to do; the
        // destination pointer is deliberately NOT carried across the
        // unlock — see flush_range. It is re-resolved after the reply.
        std::lock_guard lk(mu_);
        Allocation* a = find_locked(memory);
        if (a == nullptr || a->mapped == nullptr) {
            return VK_SUCCESS;
        }
        if (a->region_backed) {
            // Whatever the GPU wrote is already visible here.
            return VK_SUCCESS;
        }
        actual_size = resolve_size(size, a->map_size, offset - a->map_offset);
        if (actual_size == 0) {
            return VK_SUCCESS;
        }
    }

    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(device);
    payload.write_pod<std::uint64_t>(memory);
    payload.write_pod<std::uint64_t>(offset);
    payload.write_pod<std::uint64_t>(actual_size);

    ::lsw::protocol::Buffer reply;
    if (t.send_sync(::lsw::protocol::kCmdMemoryRead, payload.bytes(), &reply) !=
        ::lsw::protocol::TransportResult::Ok) {
        return VK_ERROR_DEVICE_LOST;
    }
    ::lsw::protocol::BufferReader r{reply.bytes()};
    std::int32_t rc = 0;
    if (!r.read_pod(rc)) {
        return VK_ERROR_DEVICE_LOST;
    }
    if (rc != VK_SUCCESS) {
        return static_cast<VkResult>(rc);
    }

    // Re-find under the lock: the allocation may have been unmapped
    // while the round trip was in flight, in which case there is nothing
    // to write into and the bytes are simply dropped.
    std::lock_guard lk(mu_);
    Allocation* a = find_locked(memory);
    if (a == nullptr || a->mapped == nullptr) {
        return VK_SUCCESS;
    }
    // The mapping may also have moved or shrunk; re-derive rather than
    // trusting the size we computed before the unlock.
    if (resolve_size(actual_size, a->map_size, offset - a->map_offset) != actual_size) {
        return VK_SUCCESS;
    }
    if (!r.read(a->mapped + (offset - a->map_offset), static_cast<std::size_t>(actual_size))) {
        return VK_ERROR_DEVICE_LOST;
    }
    return VK_SUCCESS;
}

}  // namespace lsw::icd
