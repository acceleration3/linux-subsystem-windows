#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace lsw::agent::transport {

class Shmem;

// The guest end of a duplex message channel in the shared region.
//
// Same messages as the socket — requests, replies and events are the
// JSON objects described in docs/link-protocol.md — carried over shared
// memory instead of TCP. That is the point: the two transports are
// interchangeable at the message level, so control traffic can move off
// TCP-over-hostfwd without a second protocol.
//
// Direction asymmetry worth knowing about: guest→host can wake the
// manager immediately (an MSI-X doorbell on the channel's vector), but
// there is no interrupt in the other direction — the manager's ivshmem
// server holds only its own eventfds. So host→guest messages are picked
// up by ``receive`` on the connection's existing tick, which is why this
// class exposes a drain rather than a callback.
class ShmemChannel {
public:
    explicit ShmemChannel(Shmem& shmem) : shmem_(&shmem) {}

    // False when the region has no RPC channel (an older manager) or the
    // device never opened. Callers fall back to the socket.
    [[nodiscard]] bool available() const;

    // Serialise and enqueue one message, then doorbell the manager.
    // False means the ring was full and the message was dropped whole —
    // never partially written.
    [[nodiscard]] bool send(const nlohmann::json& message);

    // Every complete message the manager has left for us since the last
    // call. Cheap when idle: one shared-memory read.
    [[nodiscard]] std::vector<nlohmann::json> receive();

    // Messages this side has had to drop for lack of room, cumulative.
    [[nodiscard]] std::uint64_t dropped() const;

private:
    Shmem* shmem_;
    // Reused across drains so a busy channel does not reallocate per
    // message; sized to the ring's own maximum on first use.
    std::vector<std::uint8_t> scratch_;
};

}  // namespace lsw::agent::transport
