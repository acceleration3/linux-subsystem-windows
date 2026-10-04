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
//     manager (logs, installer, console, and the session helpers' UIA).
//
// Nothing a connection attaches is torn down when it closes: UIA
// subscriptions (held by the session helpers) outlive it, and the host
// is responsible for unsubscribing them.
//
// Session-bound commands — the handlers whose Placement is UserSession —
// are not run here: the dispatcher hands them to the SessionManager,
// which forwards them to the helper in the target session.
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
class HostEndpoint {
public:
    explicit HostEndpoint(core::AgentServices& services);
    ~HostEndpoint();

    HostEndpoint(const HostEndpoint&) = delete;
    HostEndpoint& operator=(const HostEndpoint&) = delete;

    // Blocks accepting connections until ``stop_flag`` is set and
    // ``stop`` is called. Returns false if the port could not be bound —
    // which, for the service, is a failure the SCM should restart it for.
    [[nodiscard]] bool run(std::atomic<bool>& stop_flag);

    // Unblock ``run`` from another thread: closes the listening socket.
    // Connections notice ``stop_flag`` on their next tick and end.
    void stop();

private:
    [[nodiscard]] bool open_listen();
    void handle_connection(std::intptr_t accepted_fd);

    // Join threads whose connection has finished, so a long-lived agent
    // does not accumulate them.
    void reap_finished_connections();

    core::AgentServices* services_;
    // SOCKET, kept as intptr_t so this header does not have to pull in
    // <winsock2.h> (which must precede <windows.h> wherever it appears).
    std::atomic<std::intptr_t> listen_fd_{-1};
    std::atomic<bool>* stop_flag_ = nullptr;

    struct Connection {
        std::thread thread;
        std::atomic<bool> finished{false};
    };

    std::mutex connections_mu_;
    std::vector<std::unique_ptr<Connection>> connections_;
};

}  // namespace lsw::agent::transport
