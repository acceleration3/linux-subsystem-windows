#pragma once

#include "lsw/host/link/event_bus.hpp"
#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/platform/vsock_client.hpp"
#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::link {

struct AgentReply;
class PendingCalls;

// Multiplexed channel to the guest agent. One TCP connection carries
// everything: bidirectional RPCs and agent-pushed events (log records,
// install progress, UIA notifications). An application's windows do not:
// they are mirrored by a process of their own, over a connection of its
// own, and nothing of them passes through here.
//
// The framing, the message shapes and the command vocabulary are the
// shared contract in ``lsw/linkproto/`` — normative prose in
// docs/link-protocol.md. Nothing about the wire is described here,
// precisely so this file cannot drift from the agent's copy.
//
// IDs are sender-allocated per direction. Multiple concurrent in-flight
// calls are fine — replies route back to the right caller by id.
//
// Threading:
//   * One reader thread reads frames continuously, demultiplexing
//     replies (by id) and events (by topic) onto waiting callers and
//     registered handlers respectively.
//   * ``request()`` is thread-safe and non-serialized — multiple GUI /
//     worker threads can issue RPCs concurrently. Each gets its own
//     id and waits on its own condvar slot.
//   * Event handlers run on the reader thread; marshal onto the GUI
//     thread for UI work.
class AgentRpcClient : public QObject {
    Q_OBJECT
public:
    // Handler for a one-way agent-pushed event. Receives the parsed
    // frame (including the ``event`` topic field). Runs on the reader
    // thread — keep handlers fast.
    using EventHandler = EventBus::Handler;

    explicit AgentRpcClient(QObject* parent = nullptr);
    ~AgentRpcClient() override;

    // Set the target ``(cid, port)``. No-op if the target is already
    // ``(cid, port)`` — repeated calls during UI handoff don't kick
    // the connection. A real change drops the existing connection
    // and reconnects.
    void set_target(quint32 cid, quint32 port);
    void clear_target();

    [[nodiscard]] bool has_target() const noexcept {
        return cid_.load() != 0 && port_.load() != 0;
    }

    // How long ``request`` waits for a reply before giving up. A wedged
    // guest-side handler (a UIA call into a hung app, an installer
    // subprocess that never exits) would otherwise strand the caller
    // forever — and several callers are on the GUI thread, so "forever"
    // means a frozen manager with no way back.
    static constexpr std::chrono::milliseconds kDefaultCallTimeout{10000};

    // Issue the RPC ``cmd`` (a ``linkproto::cmd`` name) and block until
    // the reply arrives, the connection drops, or ``timeout`` elapses.
    // Every way that can go wrong comes back as ``ok == false`` with a
    // reason; see AgentReply.
    //
    // A reply that arrives after the timeout is discarded by the reader
    // thread: its id is no longer waited on.
    [[nodiscard]] AgentReply request(std::string_view cmd);
    [[nodiscard]] AgentReply request(std::string_view cmd, const nlohmann::json& params,
                                     std::chrono::milliseconds timeout = kDefaultCallTimeout);

    // ``request`` on a pool thread, for callers on the GUI thread.
    // ``done`` runs on ``context``'s thread, and not at all if
    // ``context`` is destroyed first. The worker holds only this client,
    // never ``context``, so closing a dialog mid-call is safe.
    void request_async(std::string_view cmd, nlohmann::json params, QObject* context,
                       std::function<void(const AgentReply&)> done);

    // ``request`` on a pool thread, its reply dropped. For commands
    // whose caller has nothing to do with the answer.
    void request_detached(std::string_view cmd, nlohmann::json params);

    // Subscribe to an agent-pushed event topic (a ``linkproto::topic``
    // name). Several handlers may share a topic; each is invoked in
    // subscription order. The handler stays registered for as long as
    // the returned subscription lives.
    [[nodiscard]] EventSubscription subscribe(std::string_view topic, EventHandler handler);

    // Bind the per-VM IVSHMEM transport, which carries the RPC ring:
    // the manager listens for the channel doorbell and drains what the
    // guest wrote, or sweeps for it when this manager did not start the
    // VM and so has no doorbells.
    void attach_shmem(vm::IvshmemServer* server, vm::ShmemRegion* region);
    void detach_shmem();

private:
    void reader_loop();

    bool ensure_connected_locked();           // caller holds io_mu_
    bool send_locked(const QByteArray& body); // caller holds io_mu_
    void drop_connection_locked();            // caller holds io_mu_

    // One message off either transport: an event to fan out, or a reply
    // for whoever is waiting on its id. The single place a message is
    // classified, whichever pipe it came down.
    void route_message(const nlohmann::json& message);

    // One control envelope off the socket. Runs on the reader thread.
    void handle_control(const std::uint8_t* payload, std::size_t len);

    mutable std::mutex io_mu_;
    platform::VsockClient client_;             // protected by io_mu_

    std::atomic<quint64> next_id_{1};

    std::unique_ptr<PendingCalls> pending_;
    std::unique_ptr<EventBus> events_;

    std::atomic<quint32> cid_{0};
    std::atomic<quint32> port_{0};
    std::atomic<quint64> generation_{0};
    quint64 last_generation_ = 0;       // protected by io_mu_

    std::atomic<bool> stop_{false};
    std::thread reader_;

    // IVSHMEM transport binding. Set by ``attach_shmem`` on the GUI
    // thread; signal connection wired then too. Cleared by
    // ``detach_shmem`` / dtor. Non-owning pointers.
    vm::IvshmemServer* shmem_server_ = nullptr;
    vm::ShmemRegion*   shmem_region_ = nullptr;

    // Doorbell-free path: drain the channel on a timer.
    //
    // Runs when the region was attached rather than created — a manager
    // that did not start the VM has no doorbells, because the ivshmem
    // device handshook with the process that did.
    Q_SLOT void poll_shmem();

    // Timer driving ``poll_shmem``; null whenever doorbells work.
    QTimer* shmem_poll_ = nullptr;

    // The guest doorbelled a message channel: drain it and route what it
    // finds exactly like socket traffic. ``kind`` is a
    // ``linkproto::kChannel*`` value.
    Q_SLOT void on_shmem_channel_ready(unsigned kind);

    // True once the guest has announced the shared-memory RPC channel.
    // Purely informational today — the socket still carries every
    // request — but it is what a later "prefer shmem" switch tests.
    std::atomic<bool> shmem_channel_live_{false};
    // Routing policy, not capability. See send_locked.
    bool prefer_shmem_rpc_ = qEnvironmentVariableIntValue("LSW_RPC_OVER_SHMEM") != 0;

    // Reused drain buffer for the shared-memory channel; sized to the
    // ring's maximum message on first use. Touched only on the GUI
    // thread (the doorbell arrives as a queued signal).
    std::vector<std::uint8_t> shmem_scratch_;
};

}  // namespace lsw::host::link
