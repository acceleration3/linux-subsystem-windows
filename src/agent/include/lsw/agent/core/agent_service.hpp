#pragma once

#include "lsw/agent/core/services.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace lsw::agent::transport {
class HostEndpoint;
}

namespace lsw::agent::core {
class AgentService {
public:
    enum class Mode { Service, Foreground };

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;

    [[nodiscard]] int run(Mode mode);

    void request_stop();

    void on_session_change(std::uint32_t session, std::uint32_t event);

    [[nodiscard]] static AgentService& instance();

private:
    AgentService();

    std::atomic<AgentServices*> services_{nullptr};
    std::atomic<bool> stop_requested_{false};

    std::mutex endpoint_mu_;
    transport::HostEndpoint* endpoint_ = nullptr;
};

}
