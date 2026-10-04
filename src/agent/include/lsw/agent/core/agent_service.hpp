#pragma once

#include "lsw/agent/core/services.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace lsw::agent::transport {
class HostEndpoint;
}

namespace lsw::agent::core {

// The agent's work as the service: the composition root that owns the
// long-lived services and runs the HostEndpoint (the RPC channel on TCP
// 5102, which also carries every push event) until a stop is requested.
//
// It runs as a LocalSystem service in session 0 (``lsw-agent``, see
// ServiceHost), or in the foreground for debugging. Either way it keeps a
// session helper in every user session (SessionManager) for what only
// works from inside one, and starts processes into whichever session a
// request names (win::RunAs).
//
// The install gate: the listener is not bound — so the manager's
// PostInstallWatcher, which treats a reachable :5102 as "install
// finished", keeps waiting, and nothing is announced on the shared
// region — until a helper in the console or wizard user's session
// reports a genuinely usable desktop (the same DesktopSession test the
// agent ran when it was started at logon). Bounded by 30 minutes, after
// which it opens anyway with a warning: an unreachable agent cannot be
// diagnosed at all.
class AgentService {
public:
    enum class Mode { Service, Foreground };

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;

    // Blocks until ``request_stop``. Returns 0 on a clean stop, non-zero
    // when the agent could not do its job (the port would not bind) —
    // which the service reports to the SCM so its recovery restarts it.
    [[nodiscard]] int run(Mode mode);

    // From any thread (the SCM's control handler).
    void request_stop();

    // A SERVICE_CONTROL_SESSIONCHANGE notification.
    void on_session_change(std::uint32_t session, std::uint32_t event);

    [[nodiscard]] static AgentService& instance();

private:
    AgentService();

    // Created in ``run``, once the mode is known; never destroyed (the
    // process ends with detached threads still referring to it). Atomic
    // because the SCM's control handler runs on another thread.
    std::atomic<AgentServices*> services_{nullptr};
    std::atomic<bool> stop_requested_{false};

    std::mutex endpoint_mu_;
    transport::HostEndpoint* endpoint_ = nullptr;
};

}  // namespace lsw::agent::core
