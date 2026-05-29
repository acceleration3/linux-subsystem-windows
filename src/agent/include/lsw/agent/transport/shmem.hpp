#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::transport {

// Guest-side IVSHMEM transport for the WGC capture path. Opens the
// virtio-win ``\\.\IVSHMEM`` device, mmaps the PCI BAR into user
// space, manages a best-fit slot allocator over the buffer area, and
// fires MSI-X doorbells to the manager when a frame is committed.
//
// Header layout is shared with the manager's ``ShmemRegion``; see
// that file for the byte-level spec. Single producer (the agent's
// capture grab path), no internal locking — the agent's host loop
// already serializes window iteration.
class Shmem {
public:
    static constexpr std::uint32_t kMagic       = 0x4657534CU;  // 'LSWF' LE
    static constexpr std::uint32_t kVersion     = 1;
    static constexpr std::uint32_t kSlotCount   = 64;
    static constexpr std::uint32_t kFormatBgra8Premul = 1;

    struct SlotInfo {
        int           index      = -1;     // -1 == invalid
        std::uint64_t buffer_off = 0;       // half-buffer half offset (front+back share buffer_size)
        std::uint64_t buffer_size = 0;     // total bytes reserved (front + back)
        std::uint32_t stride     = 0;
        std::uint32_t width      = 0;
        std::uint32_t height     = 0;
    };

    // Open + verify the device. Returns true if the BAR is mapped and
    // the LSWF header matches the manager. False is recoverable (the
    // agent falls back to TCP+JPEG once those code paths return) but
    // until Phase 6 the only consumer is the capture path which will
    // refuse to start.
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

private:
    void* device_handle_ = nullptr;  // HANDLE; ``void*`` keeps the header agnostic
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
    std::uint64_t buffers_offset_ = 0;
    std::uint64_t buffers_size_ = 0;
};

// Process-wide singleton. The capture path uses this; one IVSHMEM
// device per agent process is the whole world.
Shmem& shmem();

}  // namespace lsw::agent::transport
