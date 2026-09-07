#pragma once

#include <cstddef>
#include <cstdint>

namespace lsw::agent::transport {

// The guest's view of the manager's shared memory, through the IVSHMEM
// PCI BAR.
//
// The host half of the very same pages is the manager's ``ShmemRegion``
// — POSIX shared memory whose fd it hands to QEMU — so this is not a
// parallel implementation of anything, it is the other end of one.
//
// Two things about the mapping are worth knowing before using it:
//
//   * It is mapped WRITE-COMBINED. Writes are fast and reads are
//     dreadful, which is what the pixel path wants and what every
//     reader of it has to fence around (linkproto/region_fence.hpp).
//   * There is no interrupt from host to guest. The guest can raise an
//     MSI-X doorbell at the manager; the manager's ivshmem server holds
//     only its own eventfds and cannot ring back. So guest->host wakes
//     the peer and host->guest is polled.
//
// Windows only — a PCI function's memory is reachable on Windows solely
// through a kernel driver that has claimed the device.
class IvshmemRegion {
public:
    IvshmemRegion() = default;
    ~IvshmemRegion();

    IvshmemRegion(const IvshmemRegion&) = delete;
    IvshmemRegion& operator=(const IvshmemRegion&) = delete;

    // Find this manager's IVSHMEM device, map its BAR, and check that
    // what is there is a region this build understands.
    //
    // Every IVSHMEM function on the bus is tried, not just the first:
    // the guest also carries the render server's region, a separate
    // device whose enumeration order relative to this one is not ours
    // to decide. The region is recognised by its header rather than by
    // its position. Returns 0, or a negative errno-style code:
    //
    //   -ENODEV   no IVSHMEM device carries a region of ours (driver
    //             missing, or no <shmem/> in the domain)
    //   -ENXIO    the device is there but would not report its size
    //   -EBUSY    the device is there but would not hand out a mapping.
    //             The usual cause is that another process in this guest
    //             already has one: the driver tracks a single owner, so
    //             the agent and the ICD cannot both map the BAR.
    //   -EPROTO   a device is there but no mapping on the bus is an LSW
    //             region of this version — the expected outcome of a
    //             half-finished redeploy, and refused loudly rather
    //             than reinterpreted. Reported as -ENODEV, since from
    //             out here "no region of ours" is the same answer.
    [[nodiscard]] int open();

    // GetLastError() from whichever step failed, so a caller can say
    // more than "it did not work". Collapsing every ioctl failure into
    // one code cost a debugging round trip once already.
    [[nodiscard]] unsigned long last_os_error() const noexcept { return last_os_error_; }

    void close() noexcept;

    [[nodiscard]] void* base() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

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
    // Take ``device_handle`` (a HANDLE) as this region if what is
    // mapped through it is one of ours. Returns the same codes as
    // ``open``; on anything but 0 the handle is left for the caller to
    // close and nothing about this object has changed.
    [[nodiscard]] int adopt_device(void* device_handle);

    void* device_handle_ = nullptr;  // HANDLE; void* keeps the header agnostic
    void* base_ = nullptr;
    std::size_t size_ = 0;
    unsigned long last_os_error_ = 0;
    std::uint16_t peer_id_ = 0;
    std::uint16_t msi_vectors_ = 0;
};

}  // namespace lsw::agent::transport
