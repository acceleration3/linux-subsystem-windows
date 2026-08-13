#pragma once

#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/frame_ring.hpp"
#include "lsw/linkproto/region.hpp"
#include "lsw/linkproto/region_layout.hpp"

#include <QImage>
#include <QString>
#include <QUuid>

#include <cstdint>

namespace lsw::host::vm {

// POSIX-shm region shared with the guest through QEMU's ivshmem device.
// Layout is LSW-specific (ivshmem itself sees opaque memory) and lives
// in ``lsw/linkproto/`` — the headers the guest agent compiles too. It
// used to be described by a private list of byte offsets here and a
// second, already-divergent list on the agent side.
//
// The manager owns the layout: it creates the region, writes the channel
// table, and initialises each channel. The guest reads the table and
// takes what it finds — so adding or resizing a channel is a change on
// this side only.
//
// Channels today:
//   frames      the window-frame slot table + pixel buffers (the
//               original contents of the whole region)
//   rpc         duplex message rings, for control traffic that does not
//               want to be on TCP-over-hostfwd
//   cmdstream   duplex message rings, for the Vulkan command stream
//
// The aliases below exist so callers keep reading
// ``ShmemRegion::kSlotCount``; the values are the shared ones.
class ShmemRegion {
public:
    static constexpr quint32 kMagic     = linkproto::kFrameRingMagic;
    static constexpr quint32 kVersion   = linkproto::kRegionVersion;
    static constexpr quint32 kSlotCount = linkproto::kSlotCount;
    static constexpr quint32 kSlotSize  = sizeof(linkproto::FrameSlot);

    ShmemRegion() = default;
    ShmemRegion(const ShmemRegion&) = delete;
    ShmemRegion& operator=(const ShmemRegion&) = delete;
    ~ShmemRegion();

    // ``shm_open`` + ``ftruncate`` + ``mmap`` a fresh region for
    // ``vm_uuid`` of ``size_bytes``, then lay out and initialise every
    // channel. Returns 0 on success, -1 on failure (logged).
    [[nodiscard]] int open(const QUuid& vm_uuid, quint64 size_bytes);

    // ``munmap`` + ``close(fd)`` + ``shm_unlink``. Idempotent.
    void close();

    // The shm fd ``IvshmemServer`` sends to QEMU via SCM_RIGHTS so the
    // guest IVSHMEM device sees the same backing memory.
    [[nodiscard]] int fd() const { return fd_; }
    [[nodiscard]] quint64 size() const { return size_; }
    [[nodiscard]] QString shm_name() const { return shm_name_; }

    // Seqlock-aware slot read. Returns a fully-owned ``QImage``
    // (memcpy out of the front buffer) and the slot's hwnd. False if
    // the slot is empty or the read race-failed (caller retries on the
    // next doorbell).
    [[nodiscard]] bool read_slot_frame(int slot_index, quint64* hwnd_out,
                                       QImage* out);

    // Mark a slot empty. Called on ``frame_gone`` notification so the
    // manager forgets the hwnd; the agent should also free its
    // allocator side, but the manager invalidates eagerly so a stale
    // hwnd never paints.
    void clear_slot(int slot_index);

    // ---- Message channels ----------------------------------------------
    // Each duplex channel is two rings. The manager consumes what the
    // guest wrote and produces what the guest will read; the guest holds
    // the mirror image of these. Endpoints are invalid (``valid()`` ==
    // false) when the region is closed or the channel did not fit.

    [[nodiscard]] linkproto::ByteRingConsumer& rpc_from_guest() { return rpc_from_guest_; }
    [[nodiscard]] linkproto::ByteRingProducer& rpc_to_guest() { return rpc_to_guest_; }
    [[nodiscard]] linkproto::ByteRingConsumer& cmd_from_guest() { return cmd_from_guest_; }
    [[nodiscard]] linkproto::ByteRingProducer& cmd_to_guest() { return cmd_to_guest_; }

private:
    // Write the region header + channel table and initialise each
    // channel's contents. Returns false if the region is too small to
    // hold a usable set of channels.
    [[nodiscard]] bool lay_out_channels(quint64 size_bytes);

    // Byte address of a channel's start, or nullptr if absent.
    [[nodiscard]] std::uint8_t* channel_base(std::uint32_t kind) const;

    int     fd_       = -1;
    void*   base_     = nullptr;
    quint64 size_     = 0;
    QString shm_name_;  // shm_open name (e.g. "/lsw-frames-<uuid>")

    // Frame channel, resolved once at open. Slot and buffer offsets are
    // relative to this base, not to the region.
    std::uint8_t* frames_base_ = nullptr;
    quint64       frames_size_ = 0;

    linkproto::ByteRingConsumer rpc_from_guest_;
    linkproto::ByteRingProducer rpc_to_guest_;
    linkproto::ByteRingConsumer cmd_from_guest_;
    linkproto::ByteRingProducer cmd_to_guest_;
};

}  // namespace lsw::host::vm
