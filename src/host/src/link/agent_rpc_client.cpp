#include "lsw/host/link/agent_rpc_client.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/pending_calls.hpp"

#include "lsw/base/log.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"

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
