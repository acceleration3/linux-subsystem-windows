#include "lsw/agent/core/agent_service.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/system/driver_log_pruner.hpp"
#include "lsw/agent/system/volume_grower.hpp"
#include "lsw/agent/transport/host_endpoint.hpp"
#include "lsw/agent/win/token_privileges.hpp"

#include <chrono>
#include <memory>

namespace lsw::agent::core {

namespace {
constexpr auto kGateTimeout = std::chrono::minutes(30);

}

AgentService& AgentService::instance() {
    static auto* inst = new AgentService;
    return *inst;
}

AgentService::AgentService() = default;

int AgentService::run(Mode mode) {
    const bool as_service = mode == Mode::Service;
    LSW_AGENT_INFO("agent: starting as {}",
                   as_service ? "the lsw-agent service" : "a foreground process (debugging)");

    (void)win::TokenPrivileges::enable_install_privileges();

    auto* services = new AgentServices{AgentServices::Role::Service};
    services->attach_sessions(std::make_unique<session::SessionManager>(as_service));
    services_.store(services);
    services->sessions().start();

    const auto gate_started = std::chrono::steady_clock::now();
    if (services->sessions().wait_for_desktop(gate_started + kGateTimeout, stop_requested_)) {
        LSW_AGENT_INFO("agent: desktop usable after {}s, opening the listener",
                       std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - gate_started)
                           .count());
    } else if (!stop_requested_.load()) {
        LSW_AGENT_WARN("agent: desktop never became usable after 30 min; starting anyway");
    }
    if (stop_requested_.load()) {
        services->shutdown();
        LSW_AGENT_INFO("agent: stopped before the listener opened");
        return 0;
    }

    system::VolumeGrower::start();
    system::DriverLogPruner::run();

    LSW_AGENT_INFO("agent: starting main loop (host endpoint :5102)");
    bool listened = false;
    {
        transport::HostEndpoint endpoint{*services};
        {
            const std::lock_guard lock(endpoint_mu_);
            endpoint_ = &endpoint;
        }
        if (stop_requested_.load()) {
            endpoint.stop();
        }
        listened = endpoint.run(stop_requested_);
        {
            const std::lock_guard lock(endpoint_mu_);
            endpoint_ = nullptr;
        }
    }
    LSW_AGENT_INFO("agent: main loop exited");

    services->shutdown();
    return listened ? 0 : 1;
}

void AgentService::request_stop() {
    stop_requested_.store(true);
    const std::lock_guard lock(endpoint_mu_);
    if (endpoint_ != nullptr) {
        endpoint_->stop();
    }
}

void AgentService::on_session_change(std::uint32_t session, std::uint32_t event) {
    if (auto* services = services_.load(); services != nullptr) {
        services->sessions().notify_session_change(session, event);
    }
}

}
