#pragma once

#include "lsw/protocol/shared_region.hpp"

#include <cstdint>
#include <mutex>
#include <vector>

namespace lsw::protocol {

// Suballocates the region's ``hostmem`` channel.
//
// Phase G's point is that a host-visible Vulkan allocation does not have
// to be copied across the boundary at all: if the bytes already live in
// memory both peers have mapped, the server can import that pointer as
// ``VkDeviceMemory`` and "flush" becomes nothing.
//
// What this class does NOT do is decide whether that will work. It
// hands out offsets; the server tries the import against the real host
// pointer and answers yes or no per allocation. That split is
// deliberate — the answer depends on the driver's
// ``minImportedHostPointerAlignment`` and on where the region happened
// to be mapped, and neither of those may leak into the client, the wire
// or the region layout. A machine where the answer is always no renders
// identically, on the copy path it has today.
//
// The allocator is a first-fit free list. Allocation is not a hot path —
// applications allocate device memory at load time and map it once —
// so the simple thing is the right thing here.
class RegionArena {
public:
    // Alignment every suballocation is rounded to.
    //
    // A fixed contract constant, chosen to be at least as strict as any
    // ``minImportedHostPointerAlignment`` a driver is known to report
    // (4 KiB and 64 KiB are the values in the wild). Deliberately NOT
    // read from the local GPU: the client may run in a guest with no GPU
    // at all, and a number sampled on the host would be meaningless
    // there and machine-specific everywhere.
    static constexpr std::uint64_t kAlignment = 64ULL * 1024;

    // Allocations smaller than this stay on the copy path. Rounding a
    // 256-byte uniform buffer up to 64 KiB would spend the arena on
    // padding, and copying a few hundred bytes is cheap — the copy hurts
    // for staging buffers and vertex data, which are large.
    static constexpr std::uint64_t kMinimumUsefulSize = kAlignment;

    static constexpr std::uint64_t kNoOffset = ~std::uint64_t{0};

    RegionArena() = default;

    // Take the arena to carve. ``window`` is the hostmem channel, and
    // must outlive this object.
    void bind(SharedRegion& window) noexcept;

    [[nodiscard]] bool bound() const noexcept { return base_ != nullptr; }

    // Reserve ``bytes``. Returns an offset relative to the arena base,
    // or ``kNoOffset`` when it does not fit — which is an ordinary
    // answer meaning "use the copy path", not a failure.
    [[nodiscard]] std::uint64_t allocate(std::uint64_t bytes);

    void release(std::uint64_t offset) noexcept;

    // The client-side address of a live allocation, or nullptr.
    [[nodiscard]] std::uint8_t* pointer_at(std::uint64_t offset) noexcept;

    [[nodiscard]] std::uint64_t bytes_in_use() const noexcept;
    [[nodiscard]] std::uint64_t capacity() const noexcept { return size_; }

private:
    struct Block {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;  // rounded up to kAlignment
    };

    std::uint8_t* base_ = nullptr;
    std::uint64_t size_ = 0;

    mutable std::mutex mutex_;
    // Sorted by offset, so a first fit is a scan of the gaps between
    // neighbours and freeing needs no coalescing pass.
    std::vector<Block> live_;
};

}  // namespace lsw::protocol
