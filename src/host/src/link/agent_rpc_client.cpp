#include "lsw/host/link/agent_rpc_client.hpp"

#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include "lsw/base/log.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"
#include "lsw/linkproto/region.hpp"

#include <QMetaObject>

#include <chrono>
#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

namespace lsw::host::link {

namespace {

constexpr int kRecvChunk = 64 * 1024;
constexpr int kReconnectBackoffMs = 500;

// Sweep interval for a region with no doorbells. Fast enough that a
// mirror of a 60 Hz window misses at most one frame's worth of latency,
// and cheap enough to leave running: a sweep that finds nothing reads
// two words per slot.
constexpr int kShmemPollMs = 8;

// The envelope framing, the big-endian codecs and the command names all
// come from lsw/linkproto — the same headers the guest agent compiles.
// This file used to carry its own copy of each.
using linkproto::FrameType;
namespace cmd = linkproto::cmd;

}  // namespace

AgentRpcClient::AgentRpcClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::uint64_t>("std::uint64_t");
    qRegisterMetaType<QImage>("QImage");
    reader_ = std::thread{[this] { reader_loop(); }};
}

AgentRpcClient::~AgentRpcClient() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(io_mu_);
        drop_connection_locked();
    }
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        for (auto* p : pending_) {
            std::lock_guard<std::mutex> plock(p->mu);
            p->done = true;
            p->reply.clear();
            p->cv.notify_all();
        }
        pending_.clear();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

void AgentRpcClient::set_target(quint32 cid, quint32 port) {
    // Idempotent: if the target hasn't actually changed, don't bump
    // the generation. The wizard's SetupPage and the manager's
    // VmDetailPanel both call set_target with the same (cid, port) at
    // handoff time; bumping every call forced a reconnect that raced
    // with the reader thread (which would observe EOF on the old fd
    // and then close the freshly-opened replacement under io_mu_,
    // leaving the in-flight call() to wait on a dead connection).
    if (cid_.load() == cid && port_.load() == port) {
        return;
    }
    cid_.store(cid);
    port_.store(port);
    generation_.fetch_add(1);
}

void AgentRpcClient::clear_target() {
    set_target(0, 0);
}

void AgentRpcClient::attach_shmem(vm::IvshmemServer* server, vm::ShmemRegion* region) {
    if (shmem_server_ == server && shmem_region_ == region) {
        return;
    }
    detach_shmem();
    shmem_server_ = server;
    shmem_region_ = region;
    if (shmem_server_ != nullptr) {
        LSW_INFO("shmem: attached to ivshmem server (region={})",
                 static_cast<const void*>(shmem_region_));
        connect(shmem_server_, &vm::IvshmemServer::channel_ready, this,
                &AgentRpcClient::on_shmem_channel_ready);
        return;
    }
    if (shmem_region_ != nullptr) {
        // A region with no server: this manager attached to a VM it did
        // not start, so nothing will ring a doorbell here. Sweep instead.
        LSW_INFO("shmem: attached to a region with no doorbells — polling it every "
                 "{} ms",
                 kShmemPollMs);
        shmem_poll_ = new QTimer(this);
        shmem_poll_->setInterval(kShmemPollMs);
        connect(shmem_poll_, &QTimer::timeout, this, &AgentRpcClient::poll_shmem);
        shmem_poll_->start();
    }
}

void AgentRpcClient::poll_shmem() {
    if (shmem_region_ == nullptr) {
        return;
    }
    // The guest rings a doorbell we will never hear, so the ring is
    // drained on a sweep instead. Empty is the common case and costs
    // one load.
    on_shmem_channel_ready(linkproto::kChannelRpc);
}

void AgentRpcClient::detach_shmem() {
    if (shmem_poll_ != nullptr) {
        shmem_poll_->stop();
        shmem_poll_->deleteLater();
        shmem_poll_ = nullptr;
    }
    if (shmem_server_ != nullptr) {
        disconnect(shmem_server_, &vm::IvshmemServer::channel_ready, this,
                   &AgentRpcClient::on_shmem_channel_ready);
        shmem_server_ = nullptr;
    }
    shmem_region_ = nullptr;
    shmem_channel_live_.store(false);
}

void AgentRpcClient::on_shmem_channel_ready(unsigned kind) {
    if (shmem_region_ == nullptr || kind != linkproto::kChannelRpc) {
        // The command-stream channel has no consumer yet; its doorbell
        // is ignored rather than mistaken for control traffic.
        return;
    }
    auto& ring = shmem_region_->rpc_from_guest();
    if (!ring.valid()) {
        return;
    }
    if (shmem_scratch_.size() < ring.max_message()) {
        shmem_scratch_.resize(ring.max_message());
    }
    // Every read either consumes a record or resynchronises the ring, so
    // draining to empty terminates.
    while (!ring.empty()) {
        const std::size_t n = ring.read(shmem_scratch_.data(), shmem_scratch_.size());
        if (n == 0) {
            continue;
        }
        nlohmann::json message;
        try {
            message = nlohmann::json::parse(shmem_scratch_.data(),
                                            shmem_scratch_.data() + n);
        } catch (const nlohmann::json::exception& e) {
            LSW_WARN("agent_rpc_client: malformed shmem message: {}", e.what());
            continue;
        }
        if (!message.is_object()) {
            continue;
        }
        if (message.contains("event")) {
            const auto topic = message["event"].get<std::string>();
            if (topic == linkproto::topic::kChannelHello) {
                shmem_channel_live_.store(true);
                LSW_INFO("agent_rpc_client: guest announced the shmem RPC channel "
                         "({} KiB rings)",
                         message.value("ring_bytes", std::uint64_t{0}) / 1024);
                continue;
            }
            // Anything else is an ordinary agent event and goes to the
            // same subscribers as its socket-borne twin.
            dispatch_event(message);
            continue;
        }
        if (message.contains("id") && !message.contains("cmd")) {
            handle_control(shmem_scratch_.data(), n);
        }
    }
}

AgentRpcClient::EventToken AgentRpcClient::on_event(const QString& topic,
                                                     EventHandler handler) {
    const EventToken token = next_event_token_.fetch_add(1);
    std::lock_guard<std::mutex> lock(handlers_mu_);
    handlers_[topic].append(HandlerEntry{token, std::move(handler)});
    return token;
}

void AgentRpcClient::off_event(EventToken token) {
    if (token == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(handlers_mu_);
    for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
        auto& list = it.value();
        for (qsizetype i = 0; i < list.size(); ++i) {
            if (list[i].token == token) {
                list.removeAt(i);
                return;
            }
        }
    }
}

void AgentRpcClient::drop_connection_locked() {
    client_.close();
    // Wake any in-flight callers so they don't hang forever waiting
    // for a reply that can no longer arrive on this socket. Without
    // this, a connect attempt that *succeeded* into a half-open
    // transport (e.g. qemu hostfwd accepted the host connect but the
    // guest agent wasn't actually listening yet during VM boot)
    // leaves the call()'s pending.cv.wait stranded → kick_agent_poll's
    // ``in_flight`` flag stays true → no further polls are ever
    // attempted → the manager never detects the agent coming up
    // until it's restarted. The reader's reconnect loop is fine; the
    // pending caller just needs to be released so the next monitor
    // tick can try again.
    std::lock_guard<std::mutex> lock(pending_mu_);
    for (auto* p : pending_) {
        std::lock_guard<std::mutex> plock(p->mu);
        p->done = true;
        p->reply.clear();
        p->cv.notify_all();
    }
}

bool AgentRpcClient::ensure_connected_locked() {
    const auto gen = generation_.load();
    if (gen != last_generation_) {
        drop_connection_locked();
        last_generation_ = gen;
    }
    if (client_.is_open()) {
        return true;
    }
    const auto cid = cid_.load();
    const auto port = port_.load();
    if (cid == 0 || port == 0) {
        return false;
    }
    if (client_.connect(cid, port) != 0) {
        return false;
    }
    return true;
}

// Send one envelope frame on the wire. ``payload`` is the raw bytes
// after the 5-byte header. Caller holds io_mu_.
bool AgentRpcClient::send_locked(const QByteArray& body) {
    // Prefer the shared-memory RPC channel when it is live AND policy
    // says to use it.
    //
    // The two transports carry identical messages — same requests, same
    // replies, same ids — so this really is a routing decision and not a
    // protocol one. What it is NOT is free: the ring path has never
    // carried the control plane end to end on a real guest, and the
    // failure mode of getting it wrong is a VM that boots and then does
    // not answer. So the default stays TCP until the guest path has been
    // exercised (plan phase E3), and ``$LSW_RPC_OVER_SHMEM=1`` is how it
    // gets exercised.
    if (prefer_shmem_rpc_ && shmem_channel_live_.load() && shmem_region_ != nullptr) {
        auto& ring = shmem_region_->rpc_to_guest();
        if (ring.valid()
            && ring.write(reinterpret_cast<const std::uint8_t*>(body.constData()),
                          static_cast<std::size_t>(body.size()))) {
            // No doorbell in this direction, and there cannot be one:
            // the manager's ivshmem server holds only its OWN eventfds,
            // so there is no interrupt from host to guest. The agent
            // picks the message up on its next tick (~16 ms), which is
            // the latency this direction has and the reason the guest
            // polls rather than sleeps.
            return true;
        }
        // A full ring or a channel that went away: fall through to the
        // socket rather than dropping the request. The socket is still
        // connected — that is the whole reason it is kept.
    }

    // ``body`` is a JSON message; wrap it as a control envelope.
    if (!client_.is_open()) return false;
    std::uint8_t header[linkproto::kHeaderSize];
    linkproto::write_header(header, FrameType::Control,
                            static_cast<std::uint32_t>(body.size()));
    QByteArray framed;
    framed.reserve(static_cast<qsizetype>(linkproto::kHeaderSize) + body.size());
    framed.append(reinterpret_cast<const char*>(header),
                   static_cast<qsizetype>(linkproto::kHeaderSize));
    framed.append(body);
    return client_.send_all(framed) == 0;
}

void AgentRpcClient::post_oneway(const nlohmann::json& request) {
    if (stop_.load()) {
        return;
    }
    nlohmann::json req = request;
    req["id"] = next_id_.fetch_add(1);
    const std::string body = req.dump();
    const QByteArray bytes(body.data(), static_cast<qsizetype>(body.size()));
    std::lock_guard<std::mutex> lock(io_mu_);
    if (!ensure_connected_locked()) {
        return;  // drop — input is loss-tolerant
    }
    (void)send_locked(bytes);
}

QByteArray AgentRpcClient::call(const QByteArray& request_line,
                                 std::chrono::milliseconds timeout) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(request_line.constData(),
                                     request_line.constData() + request_line.size());
    } catch (const nlohmann::json::exception&) {
        LSW_WARN("agent_rpc_client: caller passed malformed JSON to call()");
        return {};
    }
    return call(req, timeout);
}

QByteArray AgentRpcClient::call(const nlohmann::json& request,
                                 std::chrono::milliseconds timeout) {
    if (stop_.load()) return {};
    const auto id = next_id_.fetch_add(1);
    nlohmann::json req = request;
    req["id"] = id;
    const std::string body = req.dump();
    const QByteArray bytes(body.data(), static_cast<qsizetype>(body.size()));

    Pending pending;
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        pending_.insert(id, &pending);
    }
    struct Eraser {
        AgentRpcClient* self;
        quint64 id;
        ~Eraser() {
            std::lock_guard<std::mutex> lock(self->pending_mu_);
            self->pending_.remove(id);
        }
    } eraser{this, id};

    {
        std::lock_guard<std::mutex> lock(io_mu_);
        if (!ensure_connected_locked()) {
            return {};
        }
        if (!send_locked(bytes)) {
            drop_connection_locked();
            if (!ensure_connected_locked() || !send_locked(bytes)) {
                return {};
            }
        }
    }

    std::unique_lock<std::mutex> plock(pending.mu);
    if (!pending.cv.wait_for(plock, timeout, [&] { return pending.done; })) {
        // The agent took the request and never answered. Report it with
        // the command so a wedged handler is identifiable, and return
        // empty like every other failure — the ``Eraser`` above drops
        // the id, so a late reply lands on deliver_reply's unknown-id
        // path and is discarded.
        LSW_WARN("agent_rpc_client: id={} cmd={} timed out after {} ms", id,
                 request.value("cmd", std::string{"?"}), timeout.count());
        return {};
    }
    return std::move(pending.reply);
}

void AgentRpcClient::deliver_reply(quint64 id, QByteArray body) {
    Pending* p = nullptr;
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        const auto it = pending_.constFind(id);
        if (it == pending_.constEnd()) return;
        p = it.value();
    }
    std::lock_guard<std::mutex> plock(p->mu);
    p->reply = std::move(body);
    p->done = true;
    p->cv.notify_all();
}

void AgentRpcClient::dispatch_event(const nlohmann::json& frame) {
    if (!frame.contains("event") || !frame["event"].is_string()) return;
    const auto topic = QString::fromStdString(frame["event"].get<std::string>());
    // Snapshot the handler list under the lock so we don't hold
    // handlers_mu_ while invoking — a handler that touches the rpc
    // (e.g. off_event from its own callback) would deadlock.
    QList<EventHandler> snapshot;
    {
        std::lock_guard<std::mutex> lock(handlers_mu_);
        const auto it = handlers_.constFind(topic);
        if (it == handlers_.constEnd()) return;
        snapshot.reserve(it.value().size());
        for (const auto& entry : it.value()) {
            snapshot.append(entry.handler);
        }
    }
    for (const auto& h : snapshot) {
        h(frame);
    }
}

void AgentRpcClient::handle_control(const std::uint8_t* payload, std::size_t len) {
    nlohmann::json frame;
    try {
        frame = nlohmann::json::parse(payload, payload + len);
    } catch (const nlohmann::json::exception& e) {
        LSW_WARN("agent_rpc_client: malformed control payload: {}", e.what());
        return;
    }
    if (!frame.is_object()) {
        return;
    }
    if (frame.contains("event")) {
        dispatch_event(frame);
        return;
    }
    if (!frame.contains("id")) {
        return;
    }
    if (frame.contains("cmd")) {
        return;  // agent-initiated request — none defined; drop.
    }
    const auto id = frame["id"].get<std::uint64_t>();
    QByteArray body;
    if (frame.contains("result")) {
        const auto s = frame["result"].dump();
        body = QByteArray(s.data(), static_cast<qsizetype>(s.size()));
    } else if (frame.contains("error")) {
        // Propagate the agent's error string as a parseable JSON body so
        // callers can show "agent: <msg>" instead of the empty "no reply"
        // fallback.
        const auto err_str = frame["error"].get<std::string>();
        LSW_WARN("agent_rpc_client: id={} error: {}", id, err_str);
        const auto s = nlohmann::json{{"error", err_str}}.dump();
        body = QByteArray(s.data(), static_cast<qsizetype>(s.size()));
    }
    deliver_reply(id, std::move(body));
}

void AgentRpcClient::reader_loop() {
    linkproto::FrameReader reader;
    reader.reserve(64 * 1024);

    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(io_mu_);
            if (!ensure_connected_locked()) {
                lock.unlock();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kReconnectBackoffMs));
                continue;
            }
        }
        const auto chunk = client_.recv_some(kRecvChunk);
        if (chunk.isEmpty()) {
            std::lock_guard<std::mutex> lock(io_mu_);
            drop_connection_locked();
            reader.reset();
            continue;
        }
        reader.append(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                      static_cast<std::size_t>(chunk.size()));

        (void)reader.drain([this](FrameType type, const std::uint8_t* payload,
                                   std::size_t len) {
            switch (type) {
                case FrameType::Control:
                    handle_control(payload, len);
                    break;
                default:
                    LSW_WARN("agent_rpc_client: unknown frame type={} len={}",
                             static_cast<int>(type), len);
                    break;
            }
            return true;
        });
    }
}

}  // namespace lsw::host::link
