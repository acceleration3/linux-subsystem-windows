#include "lsw/manager/vm/shmem_region.hpp"

#include "lsw/log.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

namespace lsw::manager::vm {

namespace {

// Slot field offsets (relative to slot start).
constexpr quint64 kSlotHwndOff         = 0x00;
constexpr quint64 kSlotStateOff        = 0x08;
constexpr quint64 kSlotFormatOff       = 0x0C;
constexpr quint64 kSlotWidthOff        = 0x10;
constexpr quint64 kSlotStrideOff       = 0x14;
constexpr quint64 kSlotHeightOff       = 0x18;
constexpr quint64 kSlotBufferOffsetOff = 0x20;
constexpr quint64 kSlotBufferSizeOff   = 0x28;
constexpr quint64 kSlotSeqnoOff        = 0x30;

constexpr quint32 kSlotStateActive = 1;
constexpr quint32 kFormatBgra8Premul = 1;

// Header field offsets.
constexpr quint64 kHdrMagicOff         = 0x00;
constexpr quint64 kHdrVersionOff       = 0x04;
constexpr quint64 kHdrSlotCountOff     = 0x08;
constexpr quint64 kHdrSlotSizeOff      = 0x0C;
constexpr quint64 kHdrTableOffsetOff   = 0x10;
constexpr quint64 kHdrBuffersOffsetOff = 0x18;
constexpr quint64 kHdrBuffersSizeOff   = 0x20;
constexpr quint64 kHdrRegionSizeOff    = 0x28;

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

quint64 load_seqno_acquire(const void* p) {
    std::atomic_thread_fence(std::memory_order_acquire);
    return load_relaxed<quint64>(p);
}

}  // namespace

ShmemRegion::~ShmemRegion() { close(); }

int ShmemRegion::open(const QUuid& vm_uuid, quint64 size_bytes) {
    if (fd_ >= 0) {
        LSW_WARN("ShmemRegion::open called twice; closing previous");
        close();
    }
    // Name pinned to UUID; `/` prefix is the POSIX shm convention.
    shm_name_ = QStringLiteral("/lsw-frames-%1")
                    .arg(vm_uuid.toString(QUuid::WithoutBraces));
    // O_CREAT + O_EXCL would be ideal but if the manager crashed
    // earlier the file may linger in /dev/shm; unlink-then-create
    // makes startup robust.
    const auto name_utf8 = shm_name_.toUtf8();
    ::shm_unlink(name_utf8.constData());
    fd_ = ::shm_open(name_utf8.constData(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd_ < 0) {
        LSW_ERROR("shm_open({}) failed: {}", shm_name_.toStdString(),
                  std::strerror(errno));
        return -1;
    }
    if (::ftruncate(fd_, static_cast<off_t>(size_bytes)) != 0) {
        LSW_ERROR("ftruncate({} bytes) failed: {}", size_bytes,
                  std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        ::shm_unlink(name_utf8.constData());
        return -1;
    }
    base_ = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd_, 0);
    if (base_ == MAP_FAILED) {
        LSW_ERROR("mmap({} bytes) failed: {}", size_bytes,
                  std::strerror(errno));
        base_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        ::shm_unlink(name_utf8.constData());
        return -1;
    }
    size_ = size_bytes;

    // Initialize the header. tmpfs zero-fills pages on first touch, so
    // slot table + buffer area start cleanly empty without an explicit
    // memset.
    auto* base = static_cast<std::uint8_t*>(base_);
    store_relaxed<quint32>(base + kHdrMagicOff,         kMagic);
    store_relaxed<quint32>(base + kHdrVersionOff,       kVersion);
    store_relaxed<quint32>(base + kHdrSlotCountOff,     kSlotCount);
    store_relaxed<quint32>(base + kHdrSlotSizeOff,      kSlotSize);
    store_relaxed<quint64>(base + kHdrTableOffsetOff,   kTableOffset);
    store_relaxed<quint64>(base + kHdrBuffersOffsetOff, kBuffersOffset);
    store_relaxed<quint64>(base + kHdrBuffersSizeOff,
                           size_bytes - kBuffersOffset);
    store_relaxed<quint64>(base + kHdrRegionSizeOff,    size_bytes);

    LSW_INFO("shmem region opened: {} ({} MiB, fd={})",
             shm_name_.toStdString(), size_bytes / (1024 * 1024), fd_);
    return 0;
}

void ShmemRegion::close() {
    if (base_ != nullptr) {
        ::munmap(base_, size_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!shm_name_.isEmpty()) {
        ::shm_unlink(shm_name_.toUtf8().constData());
        shm_name_.clear();
    }
    size_ = 0;
}

bool ShmemRegion::read_slot_frame(int slot_index, quint64* hwnd_out,
                                  QImage* out) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return false;
    }
    auto* base = static_cast<std::uint8_t*>(base_);
    auto* slot = base + kTableOffset
                 + static_cast<quint64>(slot_index) * kSlotSize;

    const quint64 hwnd  = load_relaxed<quint64>(slot + kSlotHwndOff);
    const quint32 state = load_relaxed<quint32>(slot + kSlotStateOff);
    if (hwnd == 0 || state != kSlotStateActive) {
        return false;
    }

    // Seqlock: spin briefly waiting for an even (= stable) seqno, then
    // copy, then re-read seqno to detect a producer race. Bounded so a
    // wedged producer can't pin the manager thread forever.
    constexpr int kMaxAttempts = 8;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const quint64 s1 = load_seqno_acquire(slot + kSlotSeqnoOff);
        if ((s1 & 1ULL) != 0) {
            continue;  // mid-write
        }
        const quint32 width        = load_relaxed<quint32>(slot + kSlotWidthOff);
        const quint32 stride       = load_relaxed<quint32>(slot + kSlotStrideOff);
        const quint32 height       = load_relaxed<quint32>(slot + kSlotHeightOff);
        const quint32 format       = load_relaxed<quint32>(slot + kSlotFormatOff);
        const quint64 buffer_off   = load_relaxed<quint64>(slot + kSlotBufferOffsetOff);
        const quint64 buffer_total = load_relaxed<quint64>(slot + kSlotBufferSizeOff);
        if (format != kFormatBgra8Premul || width == 0 || height == 0
            || stride < width * 4U || buffer_total == 0) {
            return false;
        }
        // Front buffer half is selected by ``seqno & 2`` — producer
        // toggles it after each commit.
        const quint64 half = buffer_total / 2ULL;
        const quint64 front_off = buffer_off + (((s1 & 2ULL) != 0) ? half : 0ULL);
        if (front_off + static_cast<quint64>(stride) * height > size_) {
            return false;
        }
        QImage view(base + front_off, static_cast<int>(width),
                    static_cast<int>(height), static_cast<int>(stride),
                    QImage::Format_ARGB32_Premultiplied);
        QImage copy = view.copy();  // detach from the shmem-backed bits
        std::atomic_thread_fence(std::memory_order_acquire);
        const quint64 s2 = load_relaxed<quint64>(slot + kSlotSeqnoOff);
        if (s2 == s1) {
            *hwnd_out = hwnd;
            *out = std::move(copy);
            return true;
        }
        // Producer raced; retry.
    }
    return false;
}

void ShmemRegion::clear_slot(int slot_index) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return;
    }
    auto* base = static_cast<std::uint8_t*>(base_);
    auto* slot = base + kTableOffset
                 + static_cast<quint64>(slot_index) * kSlotSize;
    store_relaxed<quint32>(slot + kSlotStateOff, 0U);
    store_relaxed<quint64>(slot + kSlotHwndOff,  0ULL);
}

}  // namespace lsw::manager::vm
