#pragma once

#include "lsw/agent/core/services.hpp"

#include <atomic>

namespace lsw::agent::core {

// Composition root for the agent process: owns the long-lived services
// and runs the HostEndpoint (the RPC channel on TCP 5102, which also
// carries every push event) until a stop is requested.
//
// The agent is autostarted by the ``lsw-agent`` scheduled task that
// LswPromote.ps1 registers (see main.cpp and autounattend.xml), so it
// runs AS THE WIZARD USER, in their session, elevated (the account is a
// local admin). Running in-session means ``EnumWindows`` and app launch
// (plain CreateProcessW) are native — no SYSTEM, no session bridging,
// and no Windows Service Control Manager hooks.
class AgentService {
public:
    AgentService();
    ~AgentService();

    AgentService(const AgentService&) = delete;
    AgentService& operator=(const AgentService&) = delete;

    // Blocks until ``request_stop`` is called from another thread
    // (Ctrl-C handler, future shutdown RPC, etc.). Returns process
    // exit code.
    [[nodiscard]] int run();

    void request_stop();

    [[nodiscard]] static AgentService& instance();

private:
    void main_loop();

    // Process-lifetime services. Constructed once at AgentService
    // construction (i.e. inside ``instance()`` on first call) and
    // explicitly ``shutdown()``-ed from ``run()`` before destruction,
    // so COM-holding members release their pointers while the COM
    // apartment is still alive.
    AgentServices services_;
    std::atomic<bool> stop_requested_{false};
};

}  // namespace lsw::agent::core
