#include "lsw/agent/core/agent_service.hpp"

#include "lsw/agent/transport/host_endpoint.hpp"
#include "lsw/agent/log.hpp"

#include <windows.h>

namespace lsw::agent::core {

namespace {

// Console Ctrl-Handler — converts Ctrl-C / window close into a clean
// stop request. The Run-key autostart launches the agent without a
// console window, so this is mostly relevant when the user runs the
// .exe by hand for debugging.
BOOL WINAPI ctrl_handler(DWORD ctrl) {
    switch (ctrl) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        AgentService::instance().request_stop();
        return TRUE;
    default:
        return FALSE;
    }
}

}  // namespace

AgentService& AgentService::instance() {
    static AgentService inst;
    return inst;
}

AgentService::AgentService() = default;

AgentService::~AgentService() {
    request_stop();
    if (endpoint_thread_.joinable()) {
        endpoint_thread_.join();
    }
}

int AgentService::run() {
    ::SetConsoleCtrlHandler(&ctrl_handler, TRUE);
    main_loop();
    // Explicit pre-destructor teardown of long-lived services. Phase
    // 6 needs this to release COM pointers while CoInitialize is
    // still in effect; Phase 3 it's a no-op but the ordering is set
    // up now so the later change doesn't have to touch this site.
    services_.shutdown();
    return 0;
}

void AgentService::request_stop() {
    stop_requested_.store(true);
}

void AgentService::main_loop() {
    // Single multiplexed listener: RPCs + log/install/frame events
    // all flow over the same connection. Window mirroring used to be
    // on a separate port (:5103); subscribe/unsubscribe are now RPCs
    // on the main channel and frames arrive as events.
    LSW_AGENT_INFO("agent: starting main loop (host endpoint :5102)");
    transport::HostEndpoint endpoint{services_};
    endpoint_thread_ = std::thread{[&]() { endpoint.run(stop_requested_); }};
    endpoint_thread_.join();
    LSW_AGENT_INFO("agent: main loop exited");
}

}  // namespace lsw::agent::core
