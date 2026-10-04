#pragma once

#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/region.hpp"

#include <atomic>
#include <cstdint>

// Where every channel goes, and who writes it.
//
// The manager owns the region layout. That was true before this header
// existed too — but the arithmetic lived inside the manager, in Qt types,
// next to logging, and there was no way to ask "what table would you
// write for a region of N bytes?" without creating a VM.
//
// That mattered the moment the local loopback started using the same
// rings: two processes on this machine must see byte-for-byte the table
// a guest would see, or the local path stops being the VM path and
// becomes a lookalike. Here the layout is one pure function of the
// region size, so the manager, the loopback and the parity test all get
// their answer from the same place instead of from three agreeing
// copies.
//
// Header-only and dependency-free on purpose: both toolchains compile it.

namespace lsw::linkproto {

// Default region size. Messages are all that travel here now, and the
// ring is a megabyte each way; the rest is room for a channel that has
// not been added yet.
//
// It lives here rather than next to either user because the layout is a
// pure function of this number: a manager that gives a VM one size and a
// loopback that creates another do not lay out the same region, and the
// local path stops being the VM path in the one way that is hardest to
// notice — everything still works, just differently.
inline constexpr int kDefaultRegionMib = 8;
inline constexpr std::uint64_t kDefaultRegionBytes =
    static_cast<std::uint64_t>(kDefaultRegionMib) << 20;

struct RegionLayout {
    ChannelView rpc;
    bool valid = false;

    [[nodiscard]] std::uint64_t required_bytes() const noexcept {
        return rpc.offset + rpc.size;
    }
};

// What the table for a region of ``size_bytes`` must say. Touches no
// memory — the layout is a function of the size and nothing else, which
// is exactly what makes it checkable.
//
// One channel today. Ascending from the first channel offset, so
// another added after it takes the space beyond.
[[nodiscard]] inline RegionLayout plan_region(std::uint64_t size_bytes) {
    RegionLayout out;
    std::uint64_t cursor = kFirstChannelOffset;

    out.rpc.kind = kChannelRpc;
    out.rpc.offset = cursor;
    out.rpc.size = kDefaultRpcChannelBytes;
    out.valid = out.rpc.offset + out.rpc.size <= size_bytes;
    return out;
}

// Does this mapping hold a region this build understands?
//
// The version check is the one that matters. The magic is deliberately
// unchanged across format changes, so a peer that knows an older one
// reads a version it does not recognise and refuses, rather than reading
// the channel table as whatever it used to be.
[[nodiscard]] inline bool region_is_readable(const void* region_base, std::uint64_t mapped_bytes) {
    if (region_base == nullptr || mapped_bytes < kFirstChannelOffset) {
        return false;
    }
    const auto* base = static_cast<const std::uint8_t*>(region_base);
    return load_field<std::uint32_t>(base + region_off::magic) == kRegionMagic
           && load_field<std::uint32_t>(base + region_off::version) == kRegionVersion
           && load_field<std::uint64_t>(base + region_off::region_size) <= mapped_bytes
           && load_field<std::uint64_t>(base + region_off::region_size) != 0;
}

// Initialise a duplex channel: two rings back to back, guest→host first.
[[nodiscard]] inline bool init_duplex_channel(void* region_base, const ChannelView& channel) {
    auto* base = static_cast<std::uint8_t*>(region_base) + channel.offset;
    const std::uint64_t ring_bytes = duplex_ring_bytes(channel.size);
    return ring_init(base + guest_to_host_ring_offset(channel.size), ring_bytes)
           && ring_init(base + host_to_guest_ring_offset(channel.size), ring_bytes);
}

// Write the region header, the channel table, and every channel's own
// contents into an already-mapped, already-zeroed region.
//
// The returned layout is invalid (and nothing is written) when the
// region is too small; ask ``plan_region(size).required_bytes()`` for
// the number to put in the error message.
//
// Callers that create the region get the zeroing for free — tmpfs and
// ``ftruncate`` zero-fill on first touch — so this deliberately does not
// memset: doing so would fault in and dirty the whole region.
[[nodiscard]] inline RegionLayout lay_out_region(void* region_base, std::uint64_t size_bytes) {
    RegionLayout layout = plan_region(size_bytes);
    if (!layout.valid || region_base == nullptr) {
        layout.valid = false;
        return layout;
    }
    auto* base = static_cast<std::uint8_t*>(region_base);

    if (!init_duplex_channel(region_base, layout.rpc)) {
        layout.valid = false;
        return layout;
    }
    write_channel(region_base, 0, layout.rpc.kind, layout.rpc.offset, layout.rpc.size);

    store_field<std::uint32_t>(base + region_off::magic, kRegionMagic);
    store_field<std::uint32_t>(base + region_off::version, kRegionVersion);
    store_field<std::uint32_t>(base + region_off::channel_count, 1U);
    store_field<std::uint32_t>(base + region_off::channel_entry_size,
                               static_cast<std::uint32_t>(sizeof(ChannelDescriptor)));
    store_field<std::uint64_t>(base + region_off::channel_table_offset, kChannelTableOffset);
    store_field<std::uint64_t>(base + region_off::region_size, size_bytes);

    // Publish only once every channel behind the table is valid: the peer
    // may be mapping the region at any moment.
    std::atomic_thread_fence(std::memory_order_release);
    return layout;
}

}  // namespace lsw::linkproto
