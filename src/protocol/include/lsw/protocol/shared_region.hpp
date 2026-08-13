#pragma once

#include <cstddef>
#include <string>

namespace lsw::protocol {

// A range of bytes two processes have both mapped.
//
// The whole of the difference between "two processes on this machine"
// and "a host and its guest" lives behind this interface:
//
//   PosixShmRegion   shm_open + mmap; both peers open the same name
//   IvshmemRegion    the guest's view of the same pages through a PCI
//                    BAR (the host side of that pair is a PosixShmRegion
//                    — it is the memory it hands to QEMU)
//
// Everything above — rings, framing, the transport, the render server —
// takes a ``SharedRegion&`` and cannot tell which it got. That is the
// point: the local path is not a simulation of the VM path, it is the
// same code with a different mapper.
class SharedRegion {
public:
    virtual ~SharedRegion() = default;

    SharedRegion() = default;
    SharedRegion(const SharedRegion&) = delete;
    SharedRegion& operator=(const SharedRegion&) = delete;
    SharedRegion(SharedRegion&&) = delete;
    SharedRegion& operator=(SharedRegion&&) = delete;

    [[nodiscard]] virtual void* base() const noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string description() const = 0;

    [[nodiscard]] bool valid() const noexcept { return base() != nullptr && size() > 0; }
};

}  // namespace lsw::protocol
