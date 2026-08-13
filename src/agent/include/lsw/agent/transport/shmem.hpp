#pragma once

#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/frame_ring.hpp"
#include "lsw/linkproto/region.hpp"
#include "lsw/protocol/ivshmem_region.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::transport {

// Guest-side IVSHMEM transport. Opens the virtio-win ``\\.\IVSHMEM``
// device, maps the PCI BAR into user space, and hands out the region's
// channels:
//
//   frames      a best-fit slot allocator over the pixel buffer area,
//               plus the doorbell fired when a frame is committed
//   rpc         duplex message rings for control traffic
//   cmdstream   duplex message rings for the Vulkan command stream
//
// The manager owns the layout and writes the channel table; this side
// reads it, so a channel that a newer manager adds costs nothing here
// until something asks for it.
//
// The layout is the shared contract in ``lsw/linkproto/`` — the same
// headers the manager compiles. Single producer on the frame path (the
// capture grab), no internal locking: the agent's host loop already
// serializes window iteration.
class Shmem {
public:
    static constexpr std::uint32_t kMagic     = linkproto::kFrameRingMagic;
    static constexpr std::uint32_t kVersion   = linkproto::kRegionVersion;
    static constexpr std::uint32_t kSlotCount = linkproto::kSlotCount;
    static constexpr std::uint32_t kFormatBgra8Premul = linkproto::kFormatBgra8Premul;

    struct SlotInfo {
        int           index      = -1;     // -1 == invalid
        std::uint64_t buffer_off = 0;       // half-buffer half offset (front+back share buffer_size)
        std::uint64_t buffer_size = 0;     // total bytes reserved (front + back)
        std::uint32_t stride     = 0;
        std::uint32_t width      = 0;
        std::uint32_t height     = 0;
    };

    // Open the device, verify the region header, and resolve every
    // channel. Returns true if the BAR is mapped and the region is one
    // this build understands.
    //
    // A version mismatch is the expected outcome of a half-finished
    // redeploy (new manager, old agent or vice versa) and is refused
    // loudly rather than reinterpreted — the frame path simply does not
    // start.
    [[nodiscard]] bool open();

    // True once ``open`` has succeeded.
    [[nodiscard]] bool available() const { return base_ != nullptr; }

    // Allocate a slot for ``hwnd`` sized w*h*4 (×2 for back buffer).
    // Returns slot.index >= 0 on success, -1 if no room.
    [[nodiscard]] SlotInfo alloc_slot(std::uint64_t hwnd, std::uint32_t width,
                                       std::uint32_t height);

    // Free a previously allocated slot (window unsubscribed / gone).
    void free_slot(int slot_index);

    // Write one frame: seqlock write of metadata + memcpy of pixel
    // data into the back buffer + doorbell. ``src_pitch`` may include
    // padding (e.g. WGC staging texture RowPitch). Returns true on
    // success, false if ``slot_index`` is invalid or geometry mismatch.
    [[nodiscard]] bool commit_frame(int slot_index, const std::uint8_t* src_bgra,
                                     std::uint32_t src_pitch,
                                     std::uint32_t width, std::uint32_t height,
                                     std::uint64_t present_qpc);

    // ---- Message channels ----------------------------------------------
    // The mirror image of the manager's endpoints: this side produces
    // guest→host and consumes host→guest. Invalid endpoints mean the
    // region has no such channel (an older manager), which callers must
    // treat as "use the socket instead", not as a failure.

    [[nodiscard]] linkproto::ByteRingProducer& rpc_to_host() { return rpc_to_host_; }
    [[nodiscard]] linkproto::ByteRingConsumer& rpc_from_host() { return rpc_from_host_; }
    [[nodiscard]] linkproto::ByteRingProducer& cmd_to_host() { return cmd_to_host_; }
    [[nodiscard]] linkproto::ByteRingConsumer& cmd_from_host() { return cmd_from_host_; }

    // Wake the manager on a channel it is waiting on. Writing to a ring
    // is silent by itself — the manager only polls after a doorbell.
    void ring_channel_doorbell(std::uint32_t channel_kind);

private:
    // Fire an MSI-X doorbell at the manager (peer 0) on ``vector``.
    void ring_doorbell(std::uint16_t vector);

    // Resolve one duplex channel into a producer/consumer pair.
    void bind_duplex_channel(std::uint32_t kind, linkproto::ByteRingProducer& to_host,
                             linkproto::ByteRingConsumer& from_host);

    // The mapping itself, shared with the guest ICD: one implementation
    // of "map the IVSHMEM BAR", not two. ``base_`` / ``size_`` are
    // cached copies of the region's, so the hot frame path is a member
    // read rather than a virtual call.
    ::lsw::protocol::IvshmemRegion region_;
    void* base_ = nullptr;
    std::uint64_t size_ = 0;
    // Allocator state: array of (offset, size, in_use) for ``kSlotCount``
    // slots, packed contiguously from ``kBuffersOffset``. Best-fit on
    // ``alloc_slot``. Single-threaded.
    struct Reservation {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        bool in_use = false;
    };
    Reservation reservations_[kSlotCount]{};

    // Frame channel. Slot and buffer offsets are relative to this base,
    // not to the region.
    std::uint8_t* frames_base_ = nullptr;
    std::uint64_t frames_size_ = 0;
    std::uint64_t buffers_offset_ = 0;  // relative to frames_base_
    std::uint64_t buffers_size_ = 0;

    linkproto::ByteRingProducer rpc_to_host_;
    linkproto::ByteRingConsumer rpc_from_host_;
    linkproto::ByteRingProducer cmd_to_host_;
    linkproto::ByteRingConsumer cmd_from_host_;
};

// Process-wide singleton. The capture path uses this; one IVSHMEM
// device per agent process is the whole world.
Shmem& shmem();

}  // namespace lsw::agent::transport
