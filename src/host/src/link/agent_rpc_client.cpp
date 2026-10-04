#include "lsw/host/link/agent_rpc_client.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/pending_calls.hpp"
#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include "lsw/base/log.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"
#include "lsw/linkproto/region.hpp"

#include <QtConcurrent>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

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

}  // namespace

AgentRpcClient::AgentRpcClient(QObject* parent)
    : QObject(parent),
      pending_(std::make_unique<PendingCalls>()),
      events_(std::make_unique<EventBus>()) {
    reader_ = std::thread{[this] { reader_loop(); }};
}

AgentRpcClient::~AgentRpcClient() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(io_mu_);
        drop_connection_locked();
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
        const auto message = nlohmann::json::parse(shmem_scratch_.data(),
                                                   shmem_scratch_.data() + n, nullptr,
                                                   /*allow_exceptions=*/false);
        if (message.is_discarded()) {
            LSW_WARN("agent_rpc_client: malformed shmem message ({} bytes)", n);
            continue;
        }
        // The one message only this pipe carries. Everything else is
        // routed exactly like its socket-borne twin.
        const auto topic = message.is_object() ? message.find("event") : message.end();
        if (topic != message.end() && *topic == linkproto::topic::kChannelHello) {
            shmem_channel_live_.store(true);
            LSW_INFO("agent_rpc_client: guest announced the shmem RPC channel "
                     "({} KiB rings)",
                     message.value("ring_bytes", std::uint64_t{0}) / 1024);
            continue;
        }
        route_message(message);
    }
}

EventSubscription AgentRpcClient::subscribe(std::string_view topic, EventHandler handler) {
    return events_->subscribe(topic, std::move(handler));
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
    pending_->fail_all(stop_.load() ? "the agent client is shutting down"
                                    : "the connection to the agent dropped");
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

AgentReply AgentRpcClient::request(std::string_view cmd) {
    return request(cmd, nlohmann::json::object());
}

AgentReply AgentRpcClient::request(std::string_view cmd, const nlohmann::json& params,
                                   std::chrono::milliseconds timeout) {
    if (stop_.load()) {
        return AgentReply::failure("the agent client is shutting down");
    }
    const auto id = next_id_.fetch_add(1);
    nlohmann::json message = {{"id", id}, {"cmd", cmd}};
    if (params.is_object() && !params.empty()) {
        message["params"] = params;
    }
    const std::string body = message.dump();
    const QByteArray bytes(body.data(), static_cast<qsizetype>(body.size()));

    // Opened once connected and just before the send, so neither the
    // connect nor a reconnect — both of which release every waiter —
    // can fail this call before it has been made.
    std::optional<PendingCalls::Ticket> ticket;
    {
        std::lock_guard<std::mutex> lock(io_mu_);
        if (!ensure_connected_locked()) {
            return AgentReply::failure(has_target() ? "agent unreachable (connect failed)"
                                                    : "agent unreachable (no VM targeted)");
        }
        ticket.emplace(*pending_, id);
        if (!send_locked(bytes)) {
            drop_connection_locked();
            ticket.reset();
            if (!ensure_connected_locked()) {
                return AgentReply::failure("agent unreachable (connection lost)");
            }
            ticket.emplace(*pending_, id);
            if (!send_locked(bytes)) {
                return AgentReply::failure("agent unreachable (send failed)");
            }
        }
    }

    auto reply = ticket->wait(timeout);
    if (!reply) {
        // The agent took the request and never answered. Report it with
        // the command so a wedged handler is identifiable; the ticket
        // closes on return, so a late reply finds nobody waiting.
        LSW_WARN("agent_rpc_client: id={} cmd={} timed out after {} ms", id, cmd,
                 timeout.count());
        return AgentReply::failure("the agent did not answer " + std::string{cmd} + " within " +
                                   std::to_string(timeout.count()) + " ms");
    }
    if (!reply->ok) {
        LSW_WARN("agent_rpc_client: id={} cmd={} failed: {}", id, cmd, reply->error);
    }
    return std::move(*reply);
}

void AgentRpcClient::request_async(std::string_view cmd, nlohmann::json params,
                                   QObject* context,
                                   std::function<void(const AgentReply&)> done) {
    QtConcurrent::run([this, cmd = std::string{cmd}, params = std::move(params)] {
        return request(cmd, params);
    }).then(context, std::move(done));
}

void AgentRpcClient::request_detached(std::string_view cmd, nlohmann::json params) {
    (void)QtConcurrent::run([this, cmd = std::string{cmd}, params = std::move(params)] {
        (void)request(cmd, params);
    });
}

void AgentRpcClient::route_message(const nlohmann::json& message) {
    if (!message.is_object()) {
        return;
    }
    if (message.contains("event")) {
        events_->dispatch(message);
        return;
    }
    if (message.contains("cmd")) {
        return;  // agent-initiated request — none defined; drop.
    }
    const auto id = message.find("id");
    if (id == message.end() || !id->is_number_unsigned()) {
        return;
    }
    pending_->deliver(id->get<std::uint64_t>(), AgentReply::from_frame(message));
}

void AgentRpcClient::handle_control(const std::uint8_t* payload, std::size_t len) {
    const auto message = nlohmann::json::parse(payload, payload + len, nullptr,
                                               /*allow_exceptions=*/false);
    if (message.is_discarded()) {
        LSW_WARN("agent_rpc_client: malformed control payload ({} bytes)", len);
        return;
    }
    route_message(message);
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
