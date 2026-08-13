#pragma once

#include "lsw/protocol/doorbell.hpp"
#include "lsw/protocol/shared_region.hpp"

#include <cstdint>
#include <string>

namespace lsw::protocol {

// The guest's view of the manager's shared memory, through the IVSHMEM
// PCI BAR.
//
// This is rule 1's second row, and the only one. The host half of the
// very same pages is a ``PosixShmRegion`` — the manager creates POSIX
// shared memory and hands the fd to QEMU — so this is not a parallel
// implementation of the local path, it is the other end of it. Above
// this class nothing changes: the same ``ShmemFrameChannel``, the same
// rings, the same framing, the same ``ChannelTransport``, the same ICD.
//
// Two things about the mapping are worth knowing before using it:
//
//   * It is mapped WRITE-COMBINED. Writes are fast and reads are
//     dreadful, which is fine for a command stream (write-only) and is
//     the reason Phase G has to decide per memory type rather than
//     assuming imported host memory is always better than a copy.
//   * There is no interrupt from host to guest. The guest can raise an
//     MSI-X doorbell at the manager; the manager's ivshmem server holds
//     only its own eventfds and cannot ring back. So guest->host wakes
//     the peer and host->guest is polled — which is exactly what
//     ``SpinPollDoorbell`` already does, and why the asymmetry costs
//     nothing above this layer.
//
// Windows only. On Linux the same region is a ``PosixShmRegion``.
class IvshmemRegion final : public SharedRegion {
public:
    IvshmemRegion() = default;
    ~IvshmemRegion() override;

    // Find the IVSHMEM device, map its BAR, and check that what is
    // there is a region this build understands. Returns 0, or a
    // negative errno-style code:
    //
    //   -ENODEV   no IVSHMEM device (driver missing, or no <shmem/> in
    //             the domain)
    //   -ENXIO    the device is there but would not report its size
    //   -EBUSY    the device is there but would not hand out a mapping.
    //             The usual cause is that another process in this guest
    //             already has one: the driver tracks a single owner, so
    //             the agent and the ICD cannot both map the BAR.
    //   -EPROTO   the mapping is not an LSW region of this version —
    //             the expected outcome of a half-finished redeploy, and
    //             refused loudly rather than reinterpreted
    [[nodiscard]] int open();

    // GetLastError() from whichever step failed, so a caller can say
    // more than "it did not work". Collapsing every ioctl failure into
    // one code cost a debugging round trip once already.
    [[nodiscard]] unsigned long last_os_error() const noexcept { return last_os_error_; }

    void close() noexcept;

    [[nodiscard]] void* base() const noexcept override { return base_; }
    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] std::string description() const override { return "ivshmem://"; }

    // Raise the MSI-X doorbell on ``vector`` at the manager (peer 0).
    void ring_vector(std::uint16_t vector) const noexcept;

    // This guest's IVSHMEM peer id, and how many MSI-X vectors the
    // device exposes. Diagnostics only — but the vector count matters:
    // too few and the channel doorbells land nowhere, so messages are
    // seen only when the manager happens to poll. A channel that looks
    // alive and never wakes its peer is worth one loud line at startup.
    [[nodiscard]] std::uint16_t peer_id() const noexcept { return peer_id_; }
    [[nodiscard]] std::uint16_t msi_vectors() const noexcept { return msi_vectors_; }

private:
    void* device_handle_ = nullptr;  // HANDLE; void* keeps the header agnostic
    void* base_ = nullptr;
    std::size_t size_ = 0;
    unsigned long last_os_error_ = 0;
    std::uint16_t peer_id_ = 0;
    std::uint16_t msi_vectors_ = 0;
};

// Wakes the manager through the region's MSI-X doorbell, and polls for
// the reply because there is no interrupt in that direction.
//
// The polling half is ``SpinPollDoorbell``'s, unchanged — a thread
// blocked waiting for a reply is not doing anything else anyway, and a
// short spin beats a syscall round trip for the latency that matters.
class IvshmemDoorbell final : public Doorbell {
public:
    IvshmemDoorbell(const IvshmemRegion& region, std::uint16_t vector) noexcept
        : region_(region), vector_(vector) {}

    void ring() noexcept override { region_.ring_vector(vector_); }

    void wait(std::chrono::microseconds timeout, bool allow_spin = true) noexcept override {
        poll_.wait(timeout, allow_spin);
    }

private:
    const IvshmemRegion& region_;
    std::uint16_t vector_;
    SpinPollDoorbell poll_;
};

}  // namespace lsw::protocol
