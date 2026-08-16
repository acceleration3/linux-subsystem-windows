#pragma once

#include "lsw/host/platform/vsock_client.hpp"
#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::link {

// Multiplexed channel to the guest agent. One TCP connection carries
// everything: bidirectional RPCs and agent-pushed events (log records,
// install progress, UIA notifications). Window pixels travel through the
// IVSHMEM frame ring instead; see ``attach_shmem``.
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
//   * ``call()`` is thread-safe and non-serialized — multiple GUI /
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
    using EventHandler = std::function<void(const nlohmann::json& frame)>;

    // Opaque cookie returned by ``on_event`` and passed back to
    // ``off_event`` to remove a single handler. 0 is reserved for
    // "invalid".
    using EventToken = std::uint64_t;

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

    // How long ``call`` waits for a reply before giving up. A wedged
    // guest-side handler (a UIA call into a hung app, an installer
    // subprocess that never exits) would otherwise strand the caller
    // forever — and several callers are on the GUI thread, so "forever"
    // means a frozen manager with no way back.
    static constexpr std::chrono::milliseconds kDefaultCallTimeout{10000};

    // Issue an RPC. Blocks until the reply arrives, the connection
    // drops, or ``timeout`` elapses. The returned bytes are the JSON
    // object inside the ``result`` field (raw — caller parses); empty on
    // failure (no target, connect failure, send/recv error, timeout, or
    // the agent returned ``error``).
    //
    // A reply that arrives after the timeout is discarded by the reader
    // thread: the id is no longer registered, which is the same path a
    // ``post_oneway`` reply takes.
    //
    // ``request_line`` may be the entire frame as a raw byte array
    // (must contain a ``cmd`` field). ``id`` is allocated and
    // injected automatically; if the caller's JSON already contains
    // an ``id`` it'll be overwritten.
    [[nodiscard]] QByteArray call(const QByteArray& request_line,
                                  std::chrono::milliseconds timeout = kDefaultCallTimeout);
    [[nodiscard]] QByteArray call(const nlohmann::json& request,
                                  std::chrono::milliseconds timeout = kDefaultCallTimeout);

    // Fire-and-forget: send a request frame and return immediately
    // without waiting for (or matching) a reply. The agent still
    // replies; the reader thread drops it (no Pending registered).
    // For high-frequency, latency-sensitive, loss-tolerant traffic
    // like input injection where a per-event blocking round-trip
    // would stall the GUI thread. An ``id`` is allocated/injected so
    // the frame is well-formed. No-op if not connected (the event is
    // simply dropped — acceptable for input).
    void post_oneway(const nlohmann::json& request);

    // Register a handler for an agent-pushed event topic. Topics
    // currently emitted by the agent:
    //   "log"             {seq, ts_ms, level, msg}
    //   "setup_progress"  {job_id, name, phase, bytes, total_bytes, ...}
    //   "setup_done"      {job_id, name, ok, exit_code, ...}
    //   "console_output"  {session_id, data_b64}
    //   "console_closed"  {session_id, exit_code}
    // Multiple handlers may register for the same topic; each one is
    // invoked in registration order. The returned ``EventToken`` must
    // be passed to ``off_event`` to remove the handler when its
    // captured objects go out of scope — otherwise the handler will
    // be called with a dangling capture.
    [[nodiscard]] EventToken on_event(const QString& topic, EventHandler handler);

    // Remove a handler previously installed via ``on_event``. Safe to
    // call with a 0 / unknown token (no-op).
    void off_event(EventToken token);

    // Window-mirror conveniences. ``subscribe_window`` fires the
    // ``subscribe_window`` RPC at the agent and remembers the HWND
    // for replay on reconnect. ``frame_received`` / ``window_gone``
    // signals fire as the agent pushes frame events back.
    void subscribe_window(std::uint64_t hwnd);
    void unsubscribe_window(std::uint64_t hwnd);

    // Bind the per-VM IVSHMEM transport. When set, the manager listens
    // to ``server->slot_ready`` and reads each frame out of
    // ``region``, then emits ``frame_received`` from the GUI thread.
    // The TCP frame-envelope path was retired in Phase 6; pixels never
    // ride this socket anymore.
    void attach_shmem(vm::IvshmemServer* server, vm::ShmemRegion* region);
    void detach_shmem();

    // Whether the guest has announced the shared-memory RPC channel.
    [[nodiscard]] bool shmem_channel_live() const noexcept {
        return shmem_channel_live_.load();
    }

    // Route requests over the shared-memory RPC channel when it is live,
    // falling back to TCP when it is not (or when the ring is full).
    //
    // Off by default, and deliberately so. The two transports carry
    // identical messages, so this is a routing choice — but the ring has
    // not yet carried the control plane end to end on a real guest, and
    // a wrong answer here is a VM that boots and then does not respond.
    // ``$LSW_RPC_OVER_SHMEM=1`` turns it on; flipping the default is the
    // last step of retiring TCP, once the guest path is proven.
    void set_prefer_shmem_rpc(bool on) noexcept { prefer_shmem_rpc_ = on; }
    [[nodiscard]] bool prefer_shmem_rpc() const noexcept { return prefer_shmem_rpc_; }

Q_SIGNALS:
    void frame_received(std::uint64_t hwnd, QImage image);
    void window_gone(std::uint64_t hwnd);

private:
    void reader_loop();

    bool ensure_connected_locked();           // caller holds io_mu_
    bool send_locked(const QByteArray& body); // caller holds io_mu_
    void drop_connection_locked();            // caller holds io_mu_
    void replay_subscriptions_locked();       // caller holds io_mu_
    void dispatch_event(const nlohmann::json& frame);
    void deliver_reply(quint64 id, QByteArray body);

    // One control envelope off the wire: an event to fan out, or a reply
    // to hand back to the waiting caller. Runs on the reader thread.
    void handle_control(const std::uint8_t* payload, std::size_t len);

    // Per-outstanding-call state held in ``pending_`` (keyed by id).
    struct Pending {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        QByteArray reply;  // contents of "result" object; empty on error
    };

    mutable std::mutex io_mu_;
    platform::VsockClient client_;             // protected by io_mu_

    std::atomic<quint64> next_id_{1};

    mutable std::mutex pending_mu_;
    QHash<quint64, Pending*> pending_;  // value-pointers; owned by caller stack

    std::atomic<quint32> cid_{0};
    std::atomic<quint32> port_{0};
    std::atomic<quint64> generation_{0};
    quint64 last_generation_ = 0;       // protected by io_mu_

    // Event handlers — written from the GUI thread (on_event /
    // off_event), read from the reader thread (dispatch_event). Each
    // topic holds a list of (token, handler) entries; dispatch
    // iterates the list in registration order. Guarded by handlers_mu_.
    struct HandlerEntry {
        EventToken token;
        EventHandler handler;
    };
    mutable std::mutex handlers_mu_;
    QHash<QString, QList<HandlerEntry>> handlers_;
    std::atomic<EventToken> next_event_token_{1};

    // Window subscriptions, replayed on reconnect. Guarded by
    // subs_mu_; touched by subscribe_window/unsubscribe_window
    // (GUI thread) and replay_subscriptions_locked (reader thread).
    mutable std::mutex subs_mu_;
    QSet<std::uint64_t> subscribed_windows_;

    std::atomic<bool> stop_{false};
    std::thread reader_;

    // IVSHMEM transport binding. Set by ``attach_shmem`` on the GUI
    // thread; signal connection wired then too. Cleared by
    // ``detach_shmem`` / dtor. Non-owning pointers.
    vm::IvshmemServer* shmem_server_ = nullptr;
    vm::ShmemRegion*   shmem_region_ = nullptr;

    Q_SLOT void on_shmem_slot_ready(int slot_index);

    // Doorbell-free path: look at every slot and every channel and act
    // on what changed since the last look.
    //
    // Runs when the region was attached rather than created — a manager
    // that did not start the VM has no doorbells, because the ivshmem
    // device handshook with the process that did. Reading a slot's
    // sequence number is two loads, so sweeping all of them costs far
    // less than the frame copy that a changed one leads to.
    Q_SLOT void poll_shmem();

    // Timer driving ``poll_shmem``; null whenever doorbells work.
    QTimer* shmem_poll_ = nullptr;
    // Last sequence number seen per slot, so a sweep only reads frames
    // that actually moved.
    std::vector<quint64> slot_seen_;

    // The guest doorbelled a message channel: drain it and route what it
    // finds exactly like socket traffic. ``kind`` is a
    // ``linkproto::kChannel*`` value.
    Q_SLOT void on_shmem_channel_ready(unsigned kind);

    // True once the guest has announced the shared-memory RPC channel.
    // Purely informational today — the socket still carries every
    // request — but it is what a later "prefer shmem" switch tests.
    std::atomic<bool> shmem_channel_live_{false};
    // Routing policy, not capability. See set_prefer_shmem_rpc.
    bool prefer_shmem_rpc_ = qEnvironmentVariableIntValue("LSW_RPC_OVER_SHMEM") != 0;

    // Reused drain buffer for the shared-memory channel; sized to the
    // ring's maximum message on first use. Touched only on the GUI
    // thread (the doorbell arrives as a queued signal).
    std::vector<std::uint8_t> shmem_scratch_;
};

}  // namespace lsw::host::link
