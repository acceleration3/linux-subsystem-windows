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

// The offsets come from the shared contract — the same headers the
// guest agent compiles. This file used to carry its own list of them.
using linkproto::load_field;

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
    created_ = true;

    LSW_INFO("shmem region opened: {} ({} MiB, fd={})",
             shm_name_.toStdString(), size_bytes / (1024 * 1024), fd_);
    return 0;
}

int ShmemRegion::attach(const QUuid& vm_uuid) {
    if (fd_ >= 0) {
        LSW_WARN("ShmemRegion::attach called on an open region; closing previous");
        close();
    }
    const QString name = QStringLiteral("/lsw-frames-%1")
                             .arg(vm_uuid.toString(QUuid::WithoutBraces));
    const auto name_utf8 = name.toUtf8();
    const int fd = ::shm_open(name_utf8.constData(), O_RDWR, 0600);
    if (fd < 0) {
        LSW_INFO("no existing shmem region {} to attach to: {}", name.toStdString(),
                 std::strerror(errno));
        return -1;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        LSW_WARN("fstat({}) failed or empty region", name.toStdString());
        ::close(fd);
        return -1;
    }
    const auto size = static_cast<quint64>(st.st_size);
    void* base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        LSW_WARN("mmap({} bytes) of {} failed: {}", size, name.toStdString(),
                 std::strerror(errno));
        ::close(fd);
        return -1;
    }

    // Only now commit to the members, so a rejected region leaves this
    // object exactly as it was.
    fd_ = fd;
    base_ = base;
    size_ = size;
    shm_name_ = name;
    created_ = false;

    const auto magic = linkproto::load_field<quint32>(
        static_cast<std::uint8_t*>(base_) + linkproto::region_off::magic);
    const auto version = linkproto::load_field<quint32>(
        static_cast<std::uint8_t*>(base_) + linkproto::region_off::version);
    if (magic != kMagic || version != kVersion) {
        LSW_WARN("shmem region {} has magic={:#x} version={} (want {:#x}/{}) — not "
                 "attaching",
                 name.toStdString(), magic, version, kMagic, kVersion);
        close();
        return -1;
    }
    if (!bind_channels_from_table()) {
        close();
        return -1;
    }

    LSW_INFO("shmem region attached: {} ({} MiB, fd={}) — reusing the region this VM "
             "is already running on",
             shm_name_.toStdString(), size_ / (1024 * 1024), fd_);
    return 0;
}

bool ShmemRegion::bind_channels_from_table() {
    const auto rpc = linkproto::find_channel(base_, linkproto::kChannelRpc);
    if (!rpc.valid()) {
        LSW_WARN("shmem region has no message channel — not one of ours");
        return false;
    }
    auto* base = static_cast<std::uint8_t*>(base_);
    rpc_from_guest_ = linkproto::ByteRingConsumer{
        base + rpc.offset + linkproto::guest_to_host_ring_offset(rpc.size)};
    rpc_to_guest_ = linkproto::ByteRingProducer{
        base + rpc.offset + linkproto::host_to_guest_ring_offset(rpc.size)};
    return true;
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
    rpc_from_guest_ = linkproto::ByteRingConsumer{
        base + layout.rpc.offset + linkproto::guest_to_host_ring_offset(layout.rpc.size)};
    rpc_to_guest_ = linkproto::ByteRingProducer{
        base + layout.rpc.offset + linkproto::host_to_guest_ring_offset(layout.rpc.size)};
    LSW_INFO("shmem channels: rpc {} KiB @0x{:x}", layout.rpc.size / 1024,
             layout.rpc.offset);
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
    if (base_ != nullptr) {
        ::munmap(base_, size_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    // The name is deliberately left in place — see the header. A region
    // whose VM is gone is unlinked by ``discard``; one whose VM is still
    // running is what the next manager attaches to.
    shm_name_.clear();
    size_ = 0;
    created_ = false;
}

void ShmemRegion::discard() {
    const QString name = shm_name_;
    close();
    if (!name.isEmpty()) {
        ::shm_unlink(name.toUtf8().constData());
        LSW_INFO("shmem region discarded: {}", name.toStdString());
    }
}

}  // namespace lsw::host::vm
