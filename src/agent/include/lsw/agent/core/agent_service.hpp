#pragma once

#include "lsw/agent/core/services.hpp"

#include <atomic>
#include <thread>

namespace lsw::agent::core {

// Composition root for the agent process: owns the HostEndpoint
// listener thread (RPC channel on TCP 5102) and the StreamEndpoint
// listener thread (multiplexed window-frame stream on TCP 5103).
//
// The agent is autostarted from ``HKLM\...\Run\lsw-agent`` (value
// written by Specialize.ps1 — see autounattend.xml). The shell runs
// Run values only at the real post-OOBE interactive logon, so the
// agent runs AS THE WIZARD USER, in their session, elevated (the
// account is a local admin; ConsentPromptBehaviorAdmin=0). Running
// in-session means ``EnumWindows`` / ``PrintWindow`` / ``GetDC`` and
// app launch (plain CreateProcessW) are all native — no SYSTEM, no
// session bridging.
//
// No Windows Service Control Manager hooks, no HKLM\Run entry.
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
    // explicitly ``shutdown()``-ed from ``run()`` before destruction
    // so any future COM-holding members release their pointers while
    // the COM apartment is still alive (Phase 6 concern).
    AgentServices services_;
    std::atomic<bool> stop_requested_{false};
    std::thread endpoint_thread_;
};

}  // namespace lsw::agent::core
