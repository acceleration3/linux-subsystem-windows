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
class HostEndpoint {
public:
    explicit HostEndpoint(core::AgentServices& services);
    ~HostEndpoint();

    HostEndpoint(const HostEndpoint&) = delete;
    HostEndpoint& operator=(const HostEndpoint&) = delete;

    [[nodiscard]] bool run(std::atomic<bool>& stop_flag);

    void stop();

private:
    [[nodiscard]] bool open_listen();
    void handle_connection(std::intptr_t accepted_fd);

    void reap_finished_connections();

    core::AgentServices* services_;
    std::atomic<std::intptr_t> listen_fd_{-1};
    std::atomic<bool>* stop_flag_ = nullptr;

    struct Connection {
        std::thread thread;
        std::atomic<bool> finished{false};
    };

    std::mutex connections_mu_;
    std::vector<std::unique_ptr<Connection>> connections_;
};

}
