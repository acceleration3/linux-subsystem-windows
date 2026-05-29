#include "lsw/agent/transport/shmem.hpp"

#include "lsw/agent/log.hpp"

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>

#include <atomic>
#include <cstring>

namespace lsw::agent::transport {

namespace {

// Public IVSHMEM driver interface GUID (from virtio-win/ivshmem/Public.h).
// We declare it locally so the agent doesn't depend on the WDK headers.
// {DF576976-569D-4672-95A0-F57E4EA0B210}
DEFINE_GUID(GUID_DEVINTERFACE_IVSHMEM,
            0xdf576976, 0x569d, 0x4672,
            0x95, 0xa0, 0xf5, 0x7e, 0x4e, 0xa0, 0xb2, 0x10);

constexpr DWORD kFileDeviceUnknown = 0x22;
constexpr DWORD kMethodBuffered    = 0;
constexpr DWORD kFileAnyAccess     = 0;

constexpr DWORD ctl_code(DWORD function) {
    return (kFileDeviceUnknown << 16) | (kFileAnyAccess << 14) | (function << 2) | kMethodBuffered;
}
// Function codes per vendor/ivshmem/ivshmem.h in the Looking Glass
// tree — these are what the installed Red Hat / virtio-win driver
// actually serves. Numbering starts at 0x800 (NOT 0x801 — that was
// the original error in this file). Verified via brute-force IOCTL
// sweep against the live driver: lower codes match the LG values.
constexpr DWORD IOCTL_IVSHMEM_REQUEST_PEERID = ctl_code(0x800);
constexpr DWORD IOCTL_IVSHMEM_REQUEST_SIZE   = ctl_code(0x801);
constexpr DWORD IOCTL_IVSHMEM_REQUEST_MMAP   = ctl_code(0x802);
constexpr DWORD IOCTL_IVSHMEM_RELEASE_MMAP   = ctl_code(0x803);
constexpr DWORD IOCTL_IVSHMEM_RING_DOORBELL  = ctl_code(0x804);

constexpr UINT8 IVSHMEM_CACHE_WRITECOMBINED = 2;

struct IvshmemMmapConfig {
    UINT8 cacheMode;
};
struct IvshmemMmap {
    UINT16 peerID;
    UINT64 size;
    PVOID  ptr;
    UINT16 vectors;
};
struct IvshmemRing {
    UINT16 peerID;
    UINT16 vector;
};

// Header offsets (mirror src/manager/src/vm/shmem_region.cpp).
constexpr std::uint64_t kHdrMagicOff         = 0x00;
constexpr std::uint64_t kHdrVersionOff       = 0x04;
constexpr std::uint64_t kHdrSlotCountOff     = 0x08;
constexpr std::uint64_t kHdrTableOffsetOff   = 0x10;
constexpr std::uint64_t kHdrBuffersOffsetOff = 0x18;
constexpr std::uint64_t kHdrBuffersSizeOff   = 0x20;

constexpr std::uint64_t kSlotSize           = 64;
constexpr std::uint64_t kSlotHwndOff        = 0x00;
constexpr std::uint64_t kSlotStateOff       = 0x08;
constexpr std::uint64_t kSlotFormatOff      = 0x0C;
constexpr std::uint64_t kSlotWidthOff       = 0x10;
constexpr std::uint64_t kSlotStrideOff      = 0x14;
constexpr std::uint64_t kSlotHeightOff      = 0x18;
constexpr std::uint64_t kSlotBufferOffOff   = 0x20;
constexpr std::uint64_t kSlotBufferSizeOff  = 0x28;
constexpr std::uint64_t kSlotSeqnoOff       = 0x30;
constexpr std::uint64_t kSlotPresentQpcOff  = 0x38;

constexpr std::uint32_t kSlotStateActive    = 1;

template <typename T>
T load_relaxed(const void* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

template <typename T>
void store_relaxed(void* p, T v) {
    std::memcpy(p, &v, sizeof(T));
}

void store_seqno_release(void* p, std::uint64_t v) {
    std::atomic_thread_fence(std::memory_order_release);
    store_relaxed<std::uint64_t>(p, v);
}

// Open the IVSHMEM device via SetupDi enumeration. Returns
// INVALID_HANDLE_VALUE on failure.
HANDLE open_ivshmem_device() {
    HDEVINFO set = ::SetupDiGetClassDevsW(&GUID_DEVINTERFACE_IVSHMEM, nullptr,
                                          nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        LSW_AGENT_WARN("shmem: SetupDiGetClassDevs failed err="
                       + std::to_string(::GetLastError()));
        return INVALID_HANDLE_VALUE;
    }
    SP_DEVICE_INTERFACE_DATA ifdata{};
    ifdata.cbSize = sizeof(ifdata);
    if (::SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_IVSHMEM,
                                      0, &ifdata) == FALSE) {
        const DWORD err = ::GetLastError();
        ::SetupDiDestroyDeviceInfoList(set);
        LSW_AGENT_WARN("shmem: SetupDiEnumDeviceInterfaces err=" + std::to_string(err)
                       + " (driver may not be installed yet)");
        return INVALID_HANDLE_VALUE;
    }
    DWORD needed = 0;
    ::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, nullptr, 0, &needed, nullptr);
    auto* detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
        ::HeapAlloc(::GetProcessHeap(), 0, needed));
    if (detail == nullptr) {
        ::SetupDiDestroyDeviceInfoList(set);
        return INVALID_HANDLE_VALUE;
    }
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (::SetupDiGetDeviceInterfaceDetailW(set, &ifdata, detail, needed, nullptr,
                                           nullptr) == FALSE) {
        ::HeapFree(::GetProcessHeap(), 0, detail);
        ::SetupDiDestroyDeviceInfoList(set);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = ::CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    ::HeapFree(::GetProcessHeap(), 0, detail);
    ::SetupDiDestroyDeviceInfoList(set);
    return h;
}

}  // namespace

Shmem& shmem() {
    static Shmem g;
    return g;
}

bool Shmem::open() {
    if (base_ != nullptr) {
        return true;
    }
    HANDLE h = open_ivshmem_device();
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    UINT64 dev_size = 0;
    DWORD got = 0;
    if (::DeviceIoControl(h, IOCTL_IVSHMEM_REQUEST_SIZE, nullptr, 0, &dev_size,
                          sizeof(dev_size), &got, nullptr) == FALSE
        || dev_size == 0) {
        LSW_AGENT_WARN("shmem: REQUEST_SIZE failed err="
                       + std::to_string(::GetLastError()));
        ::CloseHandle(h);
        return false;
    }
    IvshmemMmapConfig cfg{};
    cfg.cacheMode = IVSHMEM_CACHE_WRITECOMBINED;
    IvshmemMmap m{};
    if (::DeviceIoControl(h, IOCTL_IVSHMEM_REQUEST_MMAP, &cfg, sizeof(cfg),
                          &m, sizeof(m), &got, nullptr) == FALSE
        || m.ptr == nullptr) {
        LSW_AGENT_WARN("shmem: REQUEST_MMAP failed err="
                       + std::to_string(::GetLastError()));
        ::CloseHandle(h);
        return false;
    }

    // Verify the manager-initialized LSWF header is what we expect.
    auto* hdr = static_cast<const std::uint8_t*>(m.ptr);
    const auto magic   = load_relaxed<std::uint32_t>(hdr + kHdrMagicOff);
    const auto version = load_relaxed<std::uint32_t>(hdr + kHdrVersionOff);
    const auto scount  = load_relaxed<std::uint32_t>(hdr + kHdrSlotCountOff);
    if (magic != kMagic || version != kVersion || scount != kSlotCount) {
        LSW_AGENT_WARN("shmem: header mismatch magic=0x" + std::to_string(magic)
                       + " ver=" + std::to_string(version)
                       + " slots=" + std::to_string(scount)
                       + " (manager region not ready?)");
        ::DeviceIoControl(h, IOCTL_IVSHMEM_RELEASE_MMAP, nullptr, 0, nullptr, 0,
                          &got, nullptr);
        ::CloseHandle(h);
        return false;
    }
    buffers_offset_ = load_relaxed<std::uint64_t>(hdr + kHdrBuffersOffsetOff);
    buffers_size_   = load_relaxed<std::uint64_t>(hdr + kHdrBuffersSizeOff);

    device_handle_ = h;
    base_ = m.ptr;
    size_ = m.size;
    LSW_AGENT_INFO("shmem: opened (size=" + std::to_string(size_ / (1024 * 1024))
                   + " MiB, buffers=" + std::to_string(buffers_size_ / (1024 * 1024))
                   + " MiB, peerID=" + std::to_string(m.peerID)
                   + ", vectors=" + std::to_string(m.vectors) + ")");
    return true;
}

Shmem::SlotInfo Shmem::alloc_slot(std::uint64_t hwnd, std::uint32_t width,
                                   std::uint32_t height) {
    SlotInfo out;
    if (base_ == nullptr || width == 0 || height == 0) {
        return out;
    }
    // BGRA stride. The shmem-buffer half is sized exactly to one
    // tightly-packed frame (no padding); the agent will write at this
    // stride too. WGC's mapped staging texture may have its own
    // RowPitch, but ``commit_frame`` handles that with a per-row copy.
    const std::uint32_t stride = width * 4U;
    const std::uint64_t one_buf = static_cast<std::uint64_t>(stride) * height;
    // Round up to 64-byte alignment so each slot's halves are
    // cache-line aligned. Helps WC-memory write performance.
    const std::uint64_t aligned_buf = (one_buf + 63ULL) & ~63ULL;
    const std::uint64_t pair_size = aligned_buf * 2ULL;  // front + back

    // Best-fit allocator. Find an unused reservation whose size >=
    // pair_size (recycled), or carve out a fresh region after the
    // last reservation. Falls back to compaction if neither works.
    int best = -1;
    int free_slot = -1;
    std::uint64_t high_water = buffers_offset_;
    for (int i = 0; i < static_cast<int>(kSlotCount); ++i) {
        if (!reservations_[i].in_use) {
            if (free_slot < 0) free_slot = i;
            if (reservations_[i].size >= pair_size
                && (best < 0
                    || reservations_[i].size < reservations_[best].size)) {
                best = i;
            }
            continue;
        }
        const std::uint64_t end = reservations_[i].offset + reservations_[i].size;
        if (end > high_water) high_water = end;
    }
    int slot_idx = -1;
    std::uint64_t off = 0;
    if (best >= 0) {
        slot_idx = best;
        off = reservations_[best].offset;
    } else if (free_slot >= 0
               && high_water + pair_size <= buffers_offset_ + buffers_size_) {
        slot_idx = free_slot;
        off = high_water;
    } else {
        LSW_AGENT_WARN("shmem: alloc_slot failed (no room for "
                       + std::to_string(pair_size / 1024) + " KiB)");
        return out;
    }

    reservations_[slot_idx] = {off, pair_size, true};
    out.index       = slot_idx;
    out.buffer_off  = off;
    out.buffer_size = pair_size;
    out.stride      = stride;
    out.width       = width;
    out.height      = height;

    // Initialize the slot table entry so the manager finds it active
    // on the first doorbell.
    auto* base = static_cast<std::uint8_t*>(base_);
    auto* slot = base + 0x100ULL + static_cast<std::uint64_t>(slot_idx) * kSlotSize;
    store_relaxed<std::uint64_t>(slot + kSlotHwndOff,       hwnd);
    store_relaxed<std::uint32_t>(slot + kSlotFormatOff,     kFormatBgra8Premul);
    store_relaxed<std::uint32_t>(slot + kSlotWidthOff,      width);
    store_relaxed<std::uint32_t>(slot + kSlotStrideOff,     stride);
    store_relaxed<std::uint32_t>(slot + kSlotHeightOff,     height);
    store_relaxed<std::uint64_t>(slot + kSlotBufferOffOff,  off);
    store_relaxed<std::uint64_t>(slot + kSlotBufferSizeOff, pair_size);
    store_relaxed<std::uint64_t>(slot + kSlotSeqnoOff,      0ULL);
    store_relaxed<std::uint64_t>(slot + kSlotPresentQpcOff, 0ULL);
    std::atomic_thread_fence(std::memory_order_release);
    store_relaxed<std::uint32_t>(slot + kSlotStateOff,      kSlotStateActive);
    return out;
}

void Shmem::free_slot(int slot_index) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return;
    }
    auto* base = static_cast<std::uint8_t*>(base_);
    auto* slot = base + 0x100ULL + static_cast<std::uint64_t>(slot_index) * kSlotSize;
    // Mark empty before releasing the reservation so a racing manager
    // read returns false rather than reading a half-released slot.
    store_relaxed<std::uint32_t>(slot + kSlotStateOff, 0U);
    store_relaxed<std::uint64_t>(slot + kSlotHwndOff,  0ULL);
    reservations_[slot_index].in_use = false;
    // Keep .offset/.size so a same-or-smaller realloc reuses the
    // exact memory range (alloc's best-fit will pick it).
}

bool Shmem::commit_frame(int slot_index, const std::uint8_t* src_bgra,
                          std::uint32_t src_pitch, std::uint32_t width,
                          std::uint32_t height, std::uint64_t present_qpc) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return false;
    }
    auto& r = reservations_[slot_index];
    if (!r.in_use) {
        return false;
    }
    const std::uint32_t stride = width * 4U;
    const std::uint64_t one_buf = static_cast<std::uint64_t>(stride) * height;
    const std::uint64_t half = r.size / 2ULL;
    if (one_buf > half) {
        LSW_AGENT_WARN("shmem: commit_frame slot=" + std::to_string(slot_index)
                       + " size grew: " + std::to_string(one_buf) + " > "
                       + std::to_string(half));
        return false;
    }
    auto* base = static_cast<std::uint8_t*>(base_);
    auto* slot = base + 0x100ULL + static_cast<std::uint64_t>(slot_index) * kSlotSize;

    // Seqlock: bump to odd (mid-write), choose the OTHER half as the
    // new back buffer (so the manager keeps reading from the still-
    // valid front half), memcpy, bump to even (done) with the LSB-
    // above-1 toggled so the freshly-written half becomes front.
    const std::uint64_t s = load_relaxed<std::uint64_t>(slot + kSlotSeqnoOff);
    const std::uint64_t even_base = s & ~3ULL;   // strip parity + write bits
    const std::uint64_t new_parity = (s & 2ULL) ^ 2ULL;  // flip the half-selector
    const std::uint64_t new_seqno = even_base + 4ULL + new_parity;  // even, half toggled

    store_seqno_release(slot + kSlotSeqnoOff, new_seqno | 1ULL);  // mid-write
    const std::uint64_t back_off = r.offset + (((new_parity) != 0) ? half : 0ULL);
    std::uint8_t* dst = base + back_off;
    if (src_pitch == stride) {
        std::memcpy(dst, src_bgra, one_buf);
    } else {
        for (std::uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + static_cast<std::uint64_t>(y) * stride,
                        src_bgra + static_cast<std::uint64_t>(y) * src_pitch,
                        stride);
        }
    }
    store_relaxed<std::uint64_t>(slot + kSlotPresentQpcOff, present_qpc);
    // Update geometry every frame (window may have been resize-reallocated
    // by the allocator, but a same-size resize without a free/alloc bump
    // is also valid — width/height stay in sync).
    store_relaxed<std::uint32_t>(slot + kSlotWidthOff,  width);
    store_relaxed<std::uint32_t>(slot + kSlotStrideOff, stride);
    store_relaxed<std::uint32_t>(slot + kSlotHeightOff, height);
    store_seqno_release(slot + kSlotSeqnoOff, new_seqno);  // commit

    // Ring the manager's doorbell on this slot's vector.
    IvshmemRing ring{};
    ring.peerID = 0;  // manager is peer 0 per IvshmemServer protocol
    ring.vector = static_cast<UINT16>(slot_index);
    DWORD got = 0;
    ::DeviceIoControl(device_handle_, IOCTL_IVSHMEM_RING_DOORBELL, &ring,
                      sizeof(ring), nullptr, 0, &got, nullptr);
    return true;
}

}  // namespace lsw::agent::transport
