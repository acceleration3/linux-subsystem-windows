#include "lsw/host/vm/shmem_region.hpp"

#include "lsw/base/log.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

namespace lsw::host::vm {

namespace {

// Field offsets, the seqlock protocol and the format/state values all
// come from the shared frame-ring contract — the same header the guest
// agent compiles. This file used to carry its own list of magic offsets.
namespace slot_off = linkproto::slot_off;
using linkproto::load_field;
using linkproto::store_field;

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

    // tmpfs zero-fills pages on first touch, so every channel's tables
    // and buffers start cleanly empty without an explicit memset.
    if (!lay_out_channels(size_bytes)) {
        close();
        return -1;
    }

    LSW_INFO("shmem region opened: {} ({} MiB, fd={})",
             shm_name_.toStdString(), size_bytes / (1024 * 1024), fd_);
    return 0;
}

bool ShmemRegion::lay_out_channels(quint64 size_bytes) {
    // The arithmetic and every field it writes live in linkproto, so the
    // local loopback and the parity test lay out a region by calling the
    // same function rather than by reproducing this one.
    const auto layout = linkproto::lay_out_region(base_, size_bytes);
    if (!layout.valid) {
        LSW_ERROR("shmem region of {} MiB is too small for the channel layout "
                  "(need at least {} MiB)",
                  size_bytes / (1024 * 1024),
                  layout.required_bytes() / (1024 * 1024));
        return false;
    }

    auto* base = static_cast<std::uint8_t*>(base_);
    frames_base_ = base + layout.frames.offset;
    frames_size_ = layout.frames.size;

    rpc_from_guest_ = linkproto::ByteRingConsumer{
        base + layout.rpc.offset + linkproto::guest_to_host_ring_offset(layout.rpc.size)};
    rpc_to_guest_ = linkproto::ByteRingProducer{
        base + layout.rpc.offset + linkproto::host_to_guest_ring_offset(layout.rpc.size)};
    cmd_from_guest_ = linkproto::ByteRingConsumer{
        base + layout.cmdstream.offset
        + linkproto::guest_to_host_ring_offset(layout.cmdstream.size)};
    cmd_to_guest_ = linkproto::ByteRingProducer{
        base + layout.cmdstream.offset
        + linkproto::host_to_guest_ring_offset(layout.cmdstream.size)};

    LSW_INFO("shmem channels: rpc {} KiB @0x{:x}, cmdstream {} KiB @0x{:x}, "
             "frames {} MiB @0x{:x}",
             layout.rpc.size / 1024, layout.rpc.offset,
             layout.cmdstream.size / 1024, layout.cmdstream.offset,
             layout.frames.size / (1024 * 1024), layout.frames.offset);
    return true;
}

std::uint8_t* ShmemRegion::channel_base(std::uint32_t kind) const {
    if (base_ == nullptr) {
        return nullptr;
    }
    const auto channel = linkproto::find_channel(base_, kind);
    if (!channel.valid()) {
        return nullptr;
    }
    return static_cast<std::uint8_t*>(base_) + channel.offset;
}

void ShmemRegion::close() {
    // Endpoints point into the mapping; drop them before it goes away.
    rpc_from_guest_ = {};
    rpc_to_guest_ = {};
    cmd_from_guest_ = {};
    cmd_to_guest_ = {};
    frames_base_ = nullptr;
    frames_size_ = 0;
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
    if (frames_base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return false;
    }
    auto* base = frames_base_;
    auto* slot = linkproto::slot_ptr(frames_base_, static_cast<quint32>(slot_index));

    const quint64 hwnd  = load_field<quint64>(slot + slot_off::hwnd);
    const quint32 state = load_field<quint32>(slot + slot_off::state);
    if (hwnd == 0 || state != linkproto::kSlotStateActive) {
        LSW_DEBUG("read_slot_frame({}): hwnd={} state={} — not an active slot", slot_index,
                  hwnd, state);
        return false;
    }

    // Seqlock: spin briefly waiting for a stable seqno, then copy, then
    // re-read seqno to detect a producer race. Bounded so a wedged
    // producer can't pin the manager thread forever.
    constexpr int kMaxAttempts = 8;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const quint64 s1 = linkproto::load_seqno_acquire(slot + slot_off::seqno);
        if (linkproto::seqno_is_writing(s1)) {
            continue;
        }
        const quint32 width        = load_field<quint32>(slot + slot_off::width);
        const quint32 stride       = load_field<quint32>(slot + slot_off::stride);
        const quint32 height       = load_field<quint32>(slot + slot_off::height);
        const quint32 format       = load_field<quint32>(slot + slot_off::format);
        const quint64 buffer_off   = load_field<quint64>(slot + slot_off::buffer_offset);
        const quint64 buffer_total = load_field<quint64>(slot + slot_off::buffer_size);
        if (format != linkproto::kFormatBgra8Premul || width == 0 || height == 0
            || stride < width * 4U || buffer_total == 0) {
            LSW_DEBUG("read_slot_frame({}): rejected geometry fmt={} {}x{} stride={} buf={}",
                      slot_index, format, width, height, stride, buffer_total);
            return false;
        }
        // Which half is readable is encoded in the seqno; the producer
        // flips it on each commit.
        const quint64 front_off =
            buffer_off + linkproto::half_offset(s1, buffer_total);
        if (front_off + (static_cast<quint64>(stride) * height) > frames_size_) {
            LSW_DEBUG("read_slot_frame({}): front_off={} + {} exceeds frames_size={}",
                      slot_index, front_off,
                      static_cast<quint64>(stride) * height, frames_size_);
            return false;
        }
        QImage view(base + front_off, static_cast<int>(width),
                    static_cast<int>(height), static_cast<int>(stride),
                    QImage::Format_ARGB32_Premultiplied);
        QImage copy = view.copy();  // detach from the shmem-backed bits
        std::atomic_thread_fence(std::memory_order_acquire);
        const quint64 s2 = load_field<quint64>(slot + slot_off::seqno);
        if (s2 == s1) {
            *hwnd_out = hwnd;
            *out = std::move(copy);
            return true;
        }
        // Producer raced; retry.
    }
    LSW_DEBUG("read_slot_frame({}): gave up after {} seqlock retries", slot_index, kMaxAttempts);
    return false;
}

void ShmemRegion::clear_slot(int slot_index) {
    if (frames_base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return;
    }
    auto* slot = linkproto::slot_ptr(frames_base_, static_cast<quint32>(slot_index));
    store_field<quint32>(slot + slot_off::state, linkproto::kSlotStateEmpty);
    store_field<quint64>(slot + slot_off::hwnd,  0ULL);
}

}  // namespace lsw::host::vm
