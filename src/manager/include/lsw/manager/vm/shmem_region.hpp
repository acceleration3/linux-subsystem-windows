#pragma once

#include <QImage>
#include <QString>
#include <QUuid>

#include <cstdint>

namespace lsw::manager::vm {

// POSIX-shm region used as the WGC capture frame transport from the
// guest agent to the manager. Layout is LSW-specific (not part of
// QEMU's ivshmem protocol, which sees opaque memory) and pinned by
// the constants below.
//
// Slot layout (64 bytes each, ``kSlotCount`` slots in the table):
//   0x00  u64  hwnd                  (0 = empty slot)
//   0x08  u32  state                 (0=empty, 1=active, 2=resizing)
//   0x0C  u32  format                (1 = BGRA8 premultiplied)
//   0x10  u32  width
//   0x14  u32  stride_bytes
//   0x18  u32  height
//   0x1C  u32  pad
//   0x20  u64  buffer_offset         (front buffer; back = +buffer_size/2)
//   0x28  u64  buffer_size           (front + back; halved for either)
//   0x30  u64  seqno                 (atomic; odd = mid-write)
//   0x38  u64  present_qpc           (agent QPC at frame ready)
class ShmemRegion {
public:
    static constexpr quint32 kMagic       = 0x4657534CU;  // 'LSWF' LE
    static constexpr quint32 kVersion     = 1;
    static constexpr quint32 kSlotCount   = 64;
    static constexpr quint32 kSlotSize    = 64;
    static constexpr quint64 kTableOffset = 0x100ULL;
    static constexpr quint64 kBuffersOffset =
        kTableOffset + static_cast<quint64>(kSlotCount) * kSlotSize;

    ShmemRegion() = default;
    ShmemRegion(const ShmemRegion&) = delete;
    ShmemRegion& operator=(const ShmemRegion&) = delete;
    ~ShmemRegion();

    // ``shm_open`` + ``ftruncate`` + ``mmap`` a fresh region for
    // ``vm_uuid`` of ``size_bytes``. Writes the LSWF header so the
    // agent's handshake (Phase 4) can verify it sees the same layout.
    // Returns 0 on success, -1 on failure (logged).
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

private:
    int     fd_       = -1;
    void*   base_     = nullptr;
    quint64 size_     = 0;
    QString shm_name_;  // shm_open name (e.g. "/lsw-frames-<uuid>")
};

}  // namespace lsw::manager::vm
