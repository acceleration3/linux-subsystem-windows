#include "lsw/icd/memory_shadow.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cstdlib>
#include <cstring>

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
        std::free(a.mapped);
    }
    allocs_.clear();
}

MemoryShadow::Allocation* MemoryShadow::find_locked(std::uint64_t memory) {
    auto it = allocs_.find(memory);
    return it == allocs_.end() ? nullptr : &it->second;
}

void MemoryShadow::record_allocation(std::uint64_t memory, VkDeviceSize size) {
    std::lock_guard lk(mu_);
    allocs_[memory] = Allocation{size, nullptr, 0, 0};
}

void MemoryShadow::release_allocation(std::uint64_t memory) {
    std::lock_guard lk(mu_);
    auto it = allocs_.find(memory);
    if (it == allocs_.end()) {
        return;
    }
    std::free(it->second.mapped);
    allocs_.erase(it);
}

VkResult MemoryShadow::map(std::uint64_t memory, VkDeviceSize offset, VkDeviceSize size,
                           void** ppData) {
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
    std::uint8_t* src = nullptr;
    VkDeviceSize actual_size = 0;
    {
        std::lock_guard lk(mu_);
        Allocation* a = find_locked(memory);
        if (a == nullptr || a->mapped == nullptr) {
            return VK_SUCCESS;
        }
        actual_size = resolve_size(size, a->map_size, offset - a->map_offset);
        if (actual_size == 0) {
            return VK_SUCCESS;
        }
        src = a->mapped + (offset - a->map_offset);
    }

    ::lsw::protocol::Buffer payload;
    payload.write_pod<std::uint64_t>(device);
    payload.write_pod<std::uint64_t>(memory);
    payload.write_pod<std::uint64_t>(offset);
    payload.write_pod<std::uint64_t>(actual_size);
    payload.write(src, static_cast<std::size_t>(actual_size));

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

VkResult MemoryShadow::invalidate_range(::lsw::protocol::Transport& t, std::uint64_t device,
                                        std::uint64_t memory, VkDeviceSize offset,
                                        VkDeviceSize size) {
    std::uint8_t* dst = nullptr;
    VkDeviceSize actual_size = 0;
    {
        std::lock_guard lk(mu_);
        Allocation* a = find_locked(memory);
        if (a == nullptr || a->mapped == nullptr) {
            return VK_SUCCESS;
        }
        actual_size = resolve_size(size, a->map_size, offset - a->map_offset);
        if (actual_size == 0) {
            return VK_SUCCESS;
        }
        dst = a->mapped + (offset - a->map_offset);
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
    if (!r.read(dst, static_cast<std::size_t>(actual_size))) {
        return VK_ERROR_DEVICE_LOST;
    }
    return VK_SUCCESS;
}

}  // namespace lsw::icd
