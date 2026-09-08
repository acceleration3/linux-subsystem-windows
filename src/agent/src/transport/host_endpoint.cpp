#include "lsw/agent/transport/host_endpoint.hpp"

#include "lsw/agent/core/services.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/context.hpp"
#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/rpc/session.hpp"
#include "lsw/agent/transport/console_event_source.hpp"
#include "lsw/agent/transport/envelope_writer.hpp"
#include "lsw/agent/transport/event_pump.hpp"
#include "lsw/agent/transport/installer_event_source.hpp"
#include "lsw/agent/transport/log_event_source.hpp"
#include "lsw/agent/transport/shmem.hpp"
#include "lsw/agent/transport/shmem_channel.hpp"
#include "lsw/agent/win/window.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"
#include "lsw/linkproto/region.hpp"

// clang-format off
// Header order matters: <winsock2.h> before <windows.h>.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace lsw::agent::transport {

namespace {

// TCP port the agent listens on. The host (Linux side) reaches this via
// QEMU user-mode networking's ``hostfwd`` rule. (A real AF_VSOCK
// transport was attempted and reverted — host kernels in this class have
// no working vhost_vsock, so TCP-over-hostfwd is the reliable channel.)
constexpr unsigned short kAgentTcpPort = 5102;

// Capture poll tick. Caps latency on agent-initiated pushes and acts as
// the frame-rate cap for the window mirror (~60 Hz).
constexpr int kTickMs = 16;

constexpr std::size_t kMaxLogPayload = 200;

[[nodiscard]] std::string truncate_for_log(const std::string& s) {
    if (s.size() <= kMaxLogPayload) {
        return s;
    }
    return s.substr(0, kMaxLogPayload) + "...(+"
           + std::to_string(s.size() - kMaxLogPayload) + " more bytes)";
}

[[nodiscard]] std::string peer_address(SOCKET s) {
    sockaddr_in peer{};
    int len = sizeof(peer);
    if (::getpeername(s, reinterpret_cast<sockaddr*>(&peer), &len) != 0) {
        return "?";
    }
    char ip[64] = {};
    ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    return std::string{ip} + ":" + std::to_string(ntohs(peer.sin_port));
}

// What one connection has moved, for the line logged when it ends.
struct ConnectionStats {
    std::uint64_t bytes_received = 0;
    std::uint64_t requests_handled = 0;
    std::uint64_t events_sent = 0;

    [[nodiscard]] std::string describe(std::uint64_t bytes_sent) const {
        return std::to_string(requests_handled) + " requests, "
             + std::to_string(events_sent) + " events, "
             + std::to_string(bytes_received) + "B in / "
             + std::to_string(bytes_sent) + "B out";
    }
};

// Turns one request frame into one reply frame, logging both.
//
// Verbosity and reply summaries come from the handler itself
// (``RpcHandler::silent`` / ``summarize``) — this used to be a pair of
// functions here that switched on command names, which meant adding an
// RPC could mean editing the transport.
class RequestDispatcher {
public:
    // ``reply`` sends one JSON message back the way the request came.
    // A request that arrived over the shared-memory channel is answered
    // there, not on the socket — same messages, different pipe.
    using ReplySink = std::function<bool(const nlohmann::json&)>;

    RequestDispatcher(rpc::RpcContext& ctx, ConnectionStats& stats)
        : ctx_(&ctx), stats_(&stats) {}

    [[nodiscard]] bool handle(const nlohmann::json& request, const ReplySink& reply_sink) {
        const auto started = std::chrono::steady_clock::now();
        const auto id = request.value("id", std::uint64_t{0});
        const auto cmd = request.value("cmd", std::string{});
        const auto params = (request.contains("params") && request["params"].is_object())
                                ? request["params"]
                                : nlohmann::json::object();

        auto* handler = rpc::RpcRegistry::instance().find(cmd);
        const bool silent = (handler != nullptr) && handler->silent();
        if (!silent) {
            LSW_AGENT_INFO("host_endpoint: REQ id={} cmd={} line={}", id, cmd,
                           truncate_for_log(request.dump()));
        }

        nlohmann::json reply;
        reply["id"] = id;
        if (handler == nullptr) {
            reply["error"] = "unknown command: " + cmd;
            LSW_AGENT_WARN("host_endpoint: unknown command {}", cmd);
        } else {
            try {
                reply["result"] = handler->handle(params, *ctx_);
            } catch (const std::exception& e) {
                reply["error"] = e.what();
                LSW_AGENT_WARN("host_endpoint: cmd={} threw: {}", cmd, e.what());
            }
        }

        if (!silent) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started)
                                        .count();
            const std::string summary =
                reply.contains("error")
                    ? "error: " + reply["error"].get<std::string>()
                    : handler->summarize(reply["result"]);
            LSW_AGENT_INFO("host_endpoint: REP id={} cmd={} ({}ms) {}", id, cmd,
                           elapsed_ms, summary);
        }
        ++stats_->requests_handled;
        return reply_sink(reply);
    }

private:
    rpc::RpcContext* ctx_;
    ConnectionStats* stats_;
};

}  // namespace

HostEndpoint::HostEndpoint(core::AgentServices& services) : services_(&services) {
    WSADATA wsa{};
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
}

HostEndpoint::~HostEndpoint() {
    if (listen_fd_ >= 0) {
        ::closesocket(static_cast<SOCKET>(listen_fd_));
    }
    ::WSACleanup();
}

bool HostEndpoint::open_listen() {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LSW_AGENT_ERROR("host_endpoint: socket() failed wsa_err={}", ::WSAGetLastError());
        return false;
    }
    BOOL reuse = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kAgentTcpPort);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LSW_AGENT_ERROR("host_endpoint: bind(:{}) failed wsa_err={}", kAgentTcpPort,
                        ::WSAGetLastError());
        ::closesocket(s);
        return false;
    }
    if (::listen(s, 4) != 0) {
        LSW_AGENT_ERROR("host_endpoint: listen failed wsa_err={}", ::WSAGetLastError());
        ::closesocket(s);
        return false;
    }
    listen_fd_ = static_cast<std::intptr_t>(s);
    LSW_AGENT_INFO("host_endpoint: listening on :{}", kAgentTcpPort);
    return true;
}

void HostEndpoint::run(std::atomic<bool>& stop_flag) {
    if (!open_listen()) {
        return;
    }
    while (!stop_flag.load()) {
        sockaddr_in peer{};
        int peer_len = sizeof(peer);
        SOCKET c = ::accept(static_cast<SOCKET>(listen_fd_),
                            reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (c == INVALID_SOCKET) {
            if (stop_flag.load()) {
                break;
            }
            LSW_AGENT_WARN("host_endpoint: accept failed wsa_err={}", ::WSAGetLastError());
            continue;
        }
        // Nagle off on this side too. Replies to input are not sent at
        // all, so there is nothing to coalesce with — and the events
        // travelling the other way are exactly the small writes Nagle
        // was designed to hold back.
        {
            const int one = 1;
            if (::setsockopt(c, IPPROTO_TCP, TCP_NODELAY,
                             reinterpret_cast<const char*>(&one), sizeof(one))
                == SOCKET_ERROR) {
                LSW_AGENT_WARN("host_endpoint: TCP_NODELAY failed wsa_err={}",
                               ::WSAGetLastError());
            }
        }
        char ip[64] = {};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        const std::string peer_desc =
            std::string{ip} + ":" + std::to_string(ntohs(peer.sin_port));
        LSW_AGENT_INFO("host_endpoint: connection accepted from {}", peer_desc);

        reap_finished_connections();

        // At most one connection produces onto the region's RPC ring.
        // ``exchange`` is the claim: whoever gets false takes it.
        const bool owns_shmem = !shmem_channel_claimed_.exchange(true);

        auto conn = std::make_unique<Connection>();
        Connection* raw = conn.get();
        raw->thread = std::thread([this, c, peer_desc, owns_shmem, raw] {
            handle_connection(static_cast<std::intptr_t>(c), owns_shmem);
            ::closesocket(c);
            if (owns_shmem) {
                // Release the ring so the next connection can take it —
                // otherwise a manager restart would leave the channel
                // permanently unclaimed.
                shmem_channel_claimed_.store(false);
            }
            LSW_AGENT_INFO("host_endpoint: connection closed (peer {})", peer_desc);
            raw->finished.store(true);
        });
        {
            const std::lock_guard lk{connections_mu_};
            connections_.push_back(std::move(conn));
        }
    }

    // Stopping: let every connection thread finish before the endpoint
    // (and the services they reference) go away.
    std::vector<std::unique_ptr<Connection>> remaining;
    {
        const std::lock_guard lk{connections_mu_};
        remaining.swap(connections_);
    }
    for (auto& conn : remaining) {
        if (conn->thread.joinable()) {
            conn->thread.join();
        }
    }
}

void HostEndpoint::reap_finished_connections() {
    std::vector<std::unique_ptr<Connection>> done;
    {
        const std::lock_guard lk{connections_mu_};
        for (auto it = connections_.begin(); it != connections_.end();) {
            if ((*it)->finished.load()) {
                done.push_back(std::move(*it));
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Joined outside the lock: a thread that is on its way out may still
    // be logging, and logging takes locks of its own.
    for (auto& conn : done) {
        if (conn->thread.joinable()) {
            conn->thread.join();
        }
    }
}

void HostEndpoint::handle_connection(std::intptr_t accepted_fd, bool owns_shmem_channel) {
    auto s = static_cast<SOCKET>(accepted_fd);
    const std::string peer = peer_address(s);

    // Per-connection state. The session destructor tears down whatever
    // this connection attached (window subscriptions today, UIA
    // subscriptions as they land).
    rpc::Session session{*services_};
    rpc::RpcContext rpc_ctx{*services_, session};
    ConnectionStats stats;
    EnvelopeWriter writer{accepted_fd, peer};
    RequestDispatcher dispatcher{rpc_ctx, stats};

    // Second transport for the same messages: the shared region's RPC
    // channel. Opening the device here rather than waiting for the first
    // captured window means the channel is usable for control traffic
    // even if nothing is ever mirrored. Failure is expected and fine on
    // a guest whose IVSHMEM driver is not installed yet — the socket
    // carries everything either way.
    //
    // Only the connection that claimed the ring touches it. A second
    // connection producing onto the same single-producer ring would
    // interleave two message streams into one, and the consumer is in
    // another address space, so there is no lock that could help.
    ShmemChannel shmem_channel{shmem()};
    if (owns_shmem_channel && !shmem().available()) {
        (void)shmem().open();
    }
    const bool shmem_usable = owns_shmem_channel && shmem_channel.available();
    const auto reply_over_socket = [&](const nlohmann::json& message) {
        return writer.send_control(message);
    };
    const auto reply_over_shmem = [&](const nlohmann::json& message) {
        // A dropped reply is not fatal to the connection: the manager
        // times the call out and retries. Losing the SOCKET is.
        return shmem_channel.send(message) || true;
    };

    // Everything that pushes to the manager, in one list. Ordering is the
    // drain order: logs first so the reason for whatever follows is
    // already on record.
    LogEventSource logs;
    ConsoleEventSource console{services_->console()};
    InstallerEventSource installer{services_->installer()};
    EventPump pump;
    pump.add(logs);
    pump.add(installer);
    pump.add(console);
    pump.add(services_->uia());

    const EventPump::Sender send_event = [&](const rpc::Event& ev) {
        nlohmann::json message = ev.payload;
        message["event"] = ev.topic;
        if (!writer.send_control(message)) {
            return false;
        }
        ++stats.events_sent;
        return true;
    };

    linkproto::FrameReader reader;
    reader.reserve(64 * 1024);

    const auto on_frame = [&](linkproto::FrameType type, const std::uint8_t* payload,
                              std::size_t len) {
        if (type != linkproto::FrameType::Control) {
            return true;  // the agent expects no inbound binary frames
        }
        nlohmann::json frame;
        try {
            frame = nlohmann::json::parse(payload, payload + len);
        } catch (const nlohmann::json::exception& e) {
            LSW_AGENT_WARN("host_endpoint: malformed control JSON ({}) len={}", e.what(), len);
            return true;
        }
        if (!frame.is_object() || frame.contains("event")) {
            return true;  // manager-pushed events: none defined
        }
        if (!frame.contains("id") || !frame.contains("cmd") || !frame["cmd"].is_string()) {
            return true;
        }
        return dispatcher.handle(frame, reply_over_socket);
    };

    // Announce the channel to the manager as soon as the connection is
    // up, so the two ends agree it works before anything depends on it.
    if (shmem_usable) {
        (void)shmem_channel.send(nlohmann::json{
            {"event", linkproto::topic::kChannelHello},
            {"ring_bytes", shmem().rpc_to_host().capacity()},
        });
        LSW_AGENT_INFO("host_endpoint: shmem rpc channel available ({} KiB each way)",
                       shmem().rpc_to_host().capacity() / 1024);
    }

    // Requests arriving over the shared region are dispatched exactly
    // like socket ones and answered on the ring they came from.
    const auto drain_shmem_channel = [&] {
        if (!shmem_usable) {
            // Not our ring. Draining it here would steal requests from
            // the connection that owns it.
            return;
        }
        for (auto& message : shmem_channel.receive()) {
            if (!message.is_object() || message.contains("event")) {
                continue;
            }
            if (!message.contains("id") || !message.contains("cmd")
                || !message["cmd"].is_string()) {
                continue;
            }
            (void)dispatcher.handle(message, reply_over_shmem);
        }
    };

    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = kTickMs * 1000;
        const int ready = ::select(0, &rd, nullptr, nullptr, &tv);
        if (ready == SOCKET_ERROR) {
            LSW_AGENT_WARN("host_endpoint: select error from {} wsa_err={}", peer,
                           ::WSAGetLastError());
            return;
        }

        if (ready > 0 && FD_ISSET(s, &rd)) {
            std::uint8_t chunk[16 * 1024];
            const int n = ::recv(s, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
            if (n <= 0) {
                if (n < 0) {
                    LSW_AGENT_WARN("host_endpoint: recv error from {} wsa_err={} (after {})",
                                   peer, ::WSAGetLastError(),
                                   stats.describe(writer.bytes_sent()));
                } else {
                    LSW_AGENT_DEBUG("host_endpoint: peer {} EOF (handled {})", peer,
                                    stats.describe(writer.bytes_sent()));
                }
                return;
            }
            stats.bytes_received += static_cast<std::uint64_t>(n);
            reader.append(chunk, static_cast<std::size_t>(n));
            if (!reader.drain(on_frame)) {
                return;
            }
        }

        drain_shmem_channel();

        if (!pump.drain(send_event)) {
            return;
        }
    }
}

}  // namespace lsw::agent::transport
