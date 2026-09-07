#pragma once

#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/frame_ring.hpp"
#include "lsw/linkproto/region.hpp"
#include "lsw/linkproto/region_layout.hpp"

#include <QImage>
#include <QRect>
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

    // Map the region a previous manager left behind for ``vm_uuid`` and
    // read its channel table, writing nothing. This is how a manager
    // restarted while the VM keeps running gets back to the memory the
    // guest is already using: QEMU holds that memory open for as long as
    // the domain lives, so it outlives whichever manager created it.
    //
    // Deliberately not ``open``'s job. Laying the region out again would
    // reset every ring's head and tail underneath a guest that is
    // mid-stream, which is worse than not attaching at all. Returns 0,
    // or -1 if there is no such region or its header is not one we
    // understand (both logged).
    [[nodiscard]] int attach(const QUuid& vm_uuid);

    // ``munmap`` + ``close(fd)``. The shm name stays in the filesystem:
    // the region belongs to the VM, not to this process, and unlinking
    // it here is what used to make a manager restart cost a VM restart.
    // Idempotent.
    void close();

    // ``close`` plus ``shm_unlink`` — the region is not coming back.
    // For when the VM itself is gone; anything else should leave it for
    // the next manager to attach to.
    void discard();

    // Whether this manager created the region (as opposed to attaching
    // to one an earlier one left).
    [[nodiscard]] bool created() const { return created_; }

    // The shm fd ``IvshmemServer`` sends to QEMU via SCM_RIGHTS so the
    // guest IVSHMEM device sees the same backing memory.
    [[nodiscard]] int fd() const { return fd_; }
    [[nodiscard]] quint64 size() const { return size_; }
    [[nodiscard]] QString shm_name() const { return shm_name_; }

    // Seqlock-aware slot read. Returns a fully-owned ``QImage``
    // (memcpy out of the front buffer) and the slot's hwnd. False if
    // the slot is empty or the read race-failed (caller retries on the
    // next doorbell).
    //
    // ``visible_out`` (optional) receives the part of THAT frame which
    // is on the guest's screen — the whole image for everything but a
    // maximized window, whose frame overhangs the monitor. Read inside
    // the same seqlock as the pixels, which is the point of it being in
    // the slot at all: a rectangle from any other source can describe a
    // different frame than the one in hand.
    [[nodiscard]] bool read_slot_frame(int slot_index, quint64* hwnd_out,
                                       QImage* out, QRect* visible_out = nullptr);

    // The slot's current sequence number, or 0 for a slot with no live
    // window in it. Changes on every commit, which is all a caller
    // polling for new frames needs to compare — reading it is a couple
    // of loads, against a frame read's copy of the whole image.
    [[nodiscard]] quint64 slot_sequence(int slot_index) const;

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

private:
    // Write the region header + channel table and initialise each
    // channel's contents. Returns false if the region is too small to
    // hold a usable set of channels.
    [[nodiscard]] bool lay_out_channels(quint64 size_bytes);

    // Point the frame base and the ring endpoints at the channels the
    // table already describes, without writing anything. Shared by
    // ``attach``; ``lay_out_channels`` does the same from the layout it
    // has just written. False if the frames channel is missing, which
    // means the region is not one of ours.
    [[nodiscard]] bool bind_channels_from_table();

    // Byte address of a channel's start, or nullptr if absent.
    [[nodiscard]] std::uint8_t* channel_base(std::uint32_t kind) const;

    int     fd_       = -1;
    void*   base_     = nullptr;
    quint64 size_     = 0;
    bool    created_  = false;  // false = attached to an existing region
    QString shm_name_;  // shm_open name (e.g. "/lsw-frames-<uuid>")

    // Frame channel, resolved once at open. Slot and buffer offsets are
    // relative to this base, not to the region.
    std::uint8_t* frames_base_ = nullptr;
    quint64       frames_size_ = 0;

    linkproto::ByteRingConsumer rpc_from_guest_;
    linkproto::ByteRingProducer rpc_to_guest_;
};

}  // namespace lsw::host::vm
