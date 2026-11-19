#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/heartbeat.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <future>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

using lsw::host::link::AgentReply;
using lsw::host::link::AgentRpcClient;
namespace heartbeat = lsw::linkproto::heartbeat;

namespace {

std::atomic<int> g_failures{0};

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

std::string frame(const nlohmann::json& message) {
    const std::string body = message.dump();
    std::string out(5, '\0');
    out[0] = 1;
    out[1] = static_cast<char>(body.size() >> 24);
    out[2] = static_cast<char>(body.size() >> 16);
    out[3] = static_cast<char>(body.size() >> 8);
    out[4] = static_cast<char>(body.size());
    return out + body;
}

struct Behaviour {
    bool beat = false;
    bool answer = false;
    bool close = false;
};

class StallingAgent {
public:
    using Plan = std::function<Behaviour(int connection)>;

    explicit StallingAgent(bool beats)
        : StallingAgent(
              Plan{[beats](int connection) { return Behaviour{beats, connection > 1, false}; }}) {}

    explicit StallingAgent(Plan plan) : plan_(std::move(plan)) {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        const int one = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listener_, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread{[this] { serve(); }};
    }

    ~StallingAgent() {
        stop_.store(true);
        thread_.join();
        for (const int fd : connections_) {
            ::close(fd);
        }
        ::close(listener_);
    }

    StallingAgent(const StallingAgent&) = delete;
    StallingAgent& operator=(const StallingAgent&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    [[nodiscard]] int accepted() const noexcept { return accepted_.load(); }

private:
    void serve() {
        std::vector<pollfd> fds;
        while (!stop_.load()) {
            fds.clear();
            fds.push_back({listener_, POLLIN, 0});
            for (const int fd : connections_) {
                fds.push_back({fd, POLLIN, 0});
            }
            if (::poll(fds.data(), fds.size(), 50) <= 0) {
                continue;
            }
            if ((fds[0].revents & POLLIN) != 0) {
                const int fd = ::accept(listener_, nullptr, nullptr);
                if (fd >= 0) {
                    const int number = accepted_.fetch_add(1) + 1;
                    const Behaviour behaviour = plan_(number);
                    if (behaviour.beat) {
                        const std::string beat = frame({{"event", heartbeat::kEvent}});
                        (void)::write(fd, beat.data(), beat.size());
                    }
                    if (behaviour.close) {
                        ::close(fd);
                    } else {
                        connections_.push_back(fd);
                        if (behaviour.answer) {
                            answering_.push_back(fd);
                        }
                    }
                }
            }
            for (std::size_t i = 1; i < fds.size(); ++i) {
                if ((fds[i].revents & POLLIN) != 0) {
                    answer(fds[i].fd);
                }
            }
        }
    }

    void answer(int fd) {
        char buffer[4096];
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got <= 5 || std::find(answering_.begin(), answering_.end(), fd) == answering_.end()) {
            return;
        }
        const auto request = nlohmann::json::parse(
            std::string(buffer + 5, static_cast<std::size_t>(got - 5)), nullptr, false);
        if (!request.is_object()) {
            return;
        }
        const std::string reply = frame({{"id", request["id"]}, {"result", {{"ok", true}}}});
        (void)::write(fd, reply.data(), reply.size());
    }

    Plan plan_;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> accepted_{0};
    std::vector<int> connections_;
    std::vector<int> answering_;
    std::thread thread_;
};

void a_silent_beating_agent_is_reconnected() {
    StallingAgent agent{true};
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());

    const auto began = std::chrono::steady_clock::now();
    const auto stalled = rpc.request(lsw::linkproto::cmd::kSystemInfo, nlohmann::json::object(),
                                     heartbeat::kSilenceLimit * 3);
    const auto waited = std::chrono::steady_clock::now() - began;
    check(!stalled.ok, "a request on a connection that went silent fails");
    check(waited < heartbeat::kSilenceLimit + std::chrono::seconds{5},
          "it fails once the heartbeat has been missing for the silence limit, not at its timeout");
    const auto again = rpc.request(lsw::linkproto::cmd::kSystemInfo);
    check(again.ok, "the next request goes over a fresh connection and is answered");
    check(agent.accepted() == 2, "the client opened exactly one new connection");
}

void a_silent_agent_without_heartbeat_is_kept() {
    StallingAgent agent{false};
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());

    const auto stalled = rpc.request(lsw::linkproto::cmd::kSystemInfo, nlohmann::json::object(),
                                     heartbeat::kSilenceLimit + std::chrono::seconds{5});
    check(!stalled.ok, "an unanswered request to an agent without a heartbeat times out");
    check(agent.accepted() == 1,
          "an agent that never sent a heartbeat keeps its connection however long it is silent");
}

}  // namespace

void a_silent_new_connection_to_a_beating_agent_is_replaced() {
    StallingAgent agent{StallingAgent::Plan{[](int connection) {
        return Behaviour{connection != 2, connection == 3, connection == 1};
    }}};
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());

    const auto began = std::chrono::steady_clock::now();
    AgentReply reply;
    while (!reply.ok && std::chrono::steady_clock::now() - began < heartbeat::kSilenceLimit) {
        reply = rpc.request(lsw::linkproto::cmd::kSystemInfo, nlohmann::json::object(),
                            std::chrono::seconds{2});
    }
    const auto waited = std::chrono::steady_clock::now() - began;
    check(reply.ok && agent.accepted() == 3,
          "after the agent's connection closes, a new one that says nothing is replaced");
    check(waited < heartbeat::kFirstBeatLimit + std::chrono::seconds{3},
          "it is replaced once the first heartbeat is late, not after the full silence limit");
}

int main() {
    auto beating = std::async(std::launch::async, a_silent_beating_agent_is_reconnected);
    auto older = std::async(std::launch::async, a_silent_agent_without_heartbeat_is_kept);
    auto fresh =
        std::async(std::launch::async, a_silent_new_connection_to_a_beating_agent_is_replaced);
    beating.get();
    older.get();
    fresh.get();
    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures.load());
        return 1;
    }
    std::printf("agent heartbeat: all checks passed\n");
    return 0;
}
