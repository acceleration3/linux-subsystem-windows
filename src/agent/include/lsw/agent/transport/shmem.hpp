#pragma once

#include "lsw/agent/transport/ivshmem_region.hpp"
#include "lsw/linkproto/byte_ring.hpp"
#include "lsw/linkproto/region.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::transport {

// Guest-side IVSHMEM transport. Opens the virtio-win ``\\.\IVSHMEM``
// device, maps the PCI BAR into user space, and hands out the region's
// channels:
//
//   rpc         duplex message rings for control traffic
//
// The manager owns the layout and writes the channel table; this side
// reads it, so a channel that a newer manager adds costs nothing here
// until something asks for it.
//
// The layout is the shared contract in ``lsw/linkproto/`` — the same
// headers the manager compiles.
class Shmem {
public:
    // Open the device, verify the region header, and resolve every
    // channel. Returns true if the BAR is mapped and the region is one
    // this build understands.
    //
    // A version mismatch is the expected outcome of a half-finished
    // redeploy (new manager, old agent or vice versa) and is refused
    // loudly rather than reinterpreted.
    [[nodiscard]] bool open();

    // True once ``open`` has succeeded.
    [[nodiscard]] bool available() const { return base_ != nullptr; }

    // ---- Message channels ----------------------------------------------
    // The mirror image of the manager's endpoints: this side produces
    // guest→host and consumes host→guest. Invalid endpoints mean the
    // region has no such channel (an older manager), which callers must
    // treat as "use the socket instead", not as a failure.

    [[nodiscard]] linkproto::ByteRingProducer& rpc_to_host() { return rpc_to_host_; }

    [[nodiscard]] linkproto::ByteRingConsumer& rpc_from_host() { return rpc_from_host_; }

    // Wake the manager on the rpc channel. Writing to a ring is silent
    // by itself — the manager only polls after a doorbell.
    void ring_rpc_doorbell();

private:
    // Fire an MSI-X doorbell at the manager (peer 0) on ``vector``.
    void ring_doorbell(std::uint16_t vector);

    // Resolve one duplex channel into a producer/consumer pair.
    void bind_duplex_channel(std::uint32_t kind, linkproto::ByteRingProducer& to_host,
                             linkproto::ByteRingConsumer& from_host);

    // The mapping itself, shared with the guest ICD: one implementation
    // of "map the IVSHMEM BAR", not two. ``base_`` / ``size_`` are
    // cached copies of the region's, so the hot frame path is a member
    // read rather than a virtual call.
    IvshmemRegion region_;
    void* base_ = nullptr;
    std::uint64_t size_ = 0;
    linkproto::ByteRingProducer rpc_to_host_;
    linkproto::ByteRingConsumer rpc_from_host_;
};

// Process-wide singleton. The capture path uses this; one IVSHMEM
// device per agent process is the whole world.
Shmem& shmem();

}  // namespace lsw::agent::transport
