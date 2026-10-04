#include "lsw/agent/transport/shmem.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/ivshmem_region.hpp"

#include <cerrno>

namespace lsw::agent::transport {

Shmem& shmem() {
    static Shmem g;
    return g;
}

bool Shmem::open() {
    if (base_ != nullptr) {
        return true;
    }
    // The mapping is ``IvshmemRegion`` — the same class
    // the guest ICD uses, and the same interface the host side satisfies
    // with POSIX shared memory. This file used to open the device and
    // drive the IOCTLs itself, which meant two implementations of the
    // one thing rule 1 allows to differ between local and VM. There is
    // now one, and the agent and the ICD share it.
    if (const int rc = region_.open(); rc != 0) {
        switch (rc) {
        case -ENODEV:
            LSW_AGENT_WARN("shmem: no IVSHMEM device (driver not installed yet?)");
            break;
        case -EPROTO:
            LSW_AGENT_WARN("shmem: region header mismatch — manager and agent are "
                           "different builds? (region format v{})",
                           linkproto::kRegionVersion);
            break;
        default:
            // The OS error, not just the errno-style code: the driver
            // answers ERROR_ALREADY_INITIALIZED, ACCESS_DENIED and
            // INVALID_FUNCTION for three different causes, and
            // collapsing them into one line cost a debugging round trip
            // once already.
            LSW_AGENT_WARN("shmem: could not map the IVSHMEM BAR (rc={}, GetLastError={})", rc,
                           region_.last_os_error());
            break;
        }
        return false;
    }

    base_ = region_.base();
    size_ = region_.size();

    bind_duplex_channel(linkproto::kChannelRpc, rpc_to_host_, rpc_from_host_);

    LSW_AGENT_INFO("shmem: opened (size={} MiB, rpc_ring={} KiB, peerID={}, vectors={})",
                   size_ / (1024 * 1024), rpc_to_host_.capacity() / 1024, region_.peer_id(),
                   region_.msi_vectors());
    if (region_.msi_vectors() < linkproto::kDoorbellVectorCount) {
        // A doorbell that is never rung leaves a channel that looks
        // alive and never wakes the manager, so say so once.
        LSW_AGENT_WARN("shmem: device exposes {} MSI vectors, need {} for channel "
                       "doorbells — messages will only be seen when the manager polls",
                       region_.msi_vectors(), linkproto::kDoorbellVectorCount);
    }
    return true;
}

void Shmem::bind_duplex_channel(std::uint32_t kind, linkproto::ByteRingProducer& to_host,
                                linkproto::ByteRingConsumer& from_host) {
    const auto channel = linkproto::find_channel(base_, kind);
    if (!channel.valid()) {
        // An older manager simply has no such channel. Callers check
        // ``valid()`` and fall back to the socket.
        return;
    }
    auto* channel_base = static_cast<std::uint8_t*>(base_) + channel.offset;
    to_host = linkproto::ByteRingProducer{channel_base +
                                          linkproto::guest_to_host_ring_offset(channel.size)};
    from_host = linkproto::ByteRingConsumer{channel_base +
                                            linkproto::host_to_guest_ring_offset(channel.size)};
}

void Shmem::ring_doorbell(std::uint16_t vector) {
    region_.ring_vector(vector);
}

void Shmem::ring_rpc_doorbell() {
    ring_doorbell(linkproto::kRpcDoorbellVector);
}

}  // namespace lsw::agent::transport
