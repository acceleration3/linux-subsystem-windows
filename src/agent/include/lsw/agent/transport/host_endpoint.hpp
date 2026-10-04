#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::transport {

// TCP listener the manager connects to (reached from the Linux host
// through QEMU user-mode networking's ``hostfwd`` rule).
//
// The framing, message shapes and command vocabulary are the shared
// contract in ``lsw/linkproto/`` — normative prose in
// docs/link-protocol.md. Nothing about the wire is restated here; that
// is what let the two sides drift apart before.
//
// Per connection the endpoint owns:
//   * an ``EnvelopeWriter`` — the write half of the socket,
//   * an ``EventPump`` — every ``rpc::EventSource`` that pushes to the
//     manager (logs, installer, console, UIA).
//
// Nothing a connection attaches is torn down when it closes: UIA
// subscriptions outlive it, and the host is responsible for
// unsubscribing them.
//
// Adding a command means adding a handler TU; adding a push-event kind
// means registering one more source in ``handle_connection``. Neither
// touches the loop.
//
// Connections are served CONCURRENTLY, one thread each. They used to be
// served inline in the accept loop, one at a time, which meant MCP and
// the manager blocked each other: whichever connected first held the
// agent until it disconnected, and the other sat in a TCP backlog
// looking like a hung guest.
//
// One thing is deliberately not per-connection: the shared region's RPC
// ring. A ByteRing has exactly one producer by construction, so the
// FIRST connection to arrive claims it and the rest use the socket
// only. That is a real constraint of the transport rather than a policy
// — two producers on one ring is not a thing that can be made to work
// with a lock, because the reader is in another address space.
class HostEndpoint {
public:
    explicit HostEndpoint(core::AgentServices& services);
    ~HostEndpoint();

    HostEndpoint(const HostEndpoint&) = delete;
    HostEndpoint& operator=(const HostEndpoint&) = delete;

    // Blocks accepting connections until ``stop_flag`` is set. ``run``
    // is the only entry — fully self-contained, owns its socket fd.
    void run(std::atomic<bool>& stop_flag);

private:
    [[nodiscard]] bool open_listen();
    // ``owns_shmem_channel`` is true for the one connection that claimed
    // the region's RPC ring.
    void handle_connection(std::intptr_t accepted_fd, bool owns_shmem_channel);

    // Join threads whose connection has finished, so a long-lived agent
    // does not accumulate them.
    void reap_finished_connections();

    core::AgentServices* services_;
    // SOCKET, kept as intptr_t so this header does not have to pull in
    // <winsock2.h> (which must precede <windows.h> wherever it appears).
    std::intptr_t listen_fd_ = -1;

    struct Connection {
        std::thread thread;
        std::atomic<bool> finished{false};
    };

    std::mutex connections_mu_;
    std::vector<std::unique_ptr<Connection>> connections_;

    // Cleared when the owning connection ends, so the next one to
    // connect picks the ring up.
    std::atomic<bool> shmem_channel_claimed_{false};
};

}  // namespace lsw::agent::transport
