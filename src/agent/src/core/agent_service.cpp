#include "lsw/agent/core/agent_service.hpp"

#include "lsw/agent/apps/guest_drivers.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/host_endpoint.hpp"

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

AgentService::~AgentService() = default;

int AgentService::run() {
    ::SetConsoleCtrlHandler(&ctrl_handler, TRUE);
    // Before anything can be launched. The OpenCL loader reads this at
    // the first clGetPlatformIDs of each process, so an application
    // started before it is written finds no driver.
    apps::GuestDrivers::register_installed_icds();
    main_loop();
    // Explicit pre-destructor teardown of long-lived services: UIA
    // detaches its subscriptions and releases its COM pointers while
    // CoInitialize is still in effect.
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
    // On this thread: it has nothing else to do until the endpoint
    // returns. Connections get threads of their own inside ``run``.
    transport::HostEndpoint endpoint{services_};
    endpoint.run(stop_requested_);
    LSW_AGENT_INFO("agent: main loop exited");
}

}  // namespace lsw::agent::core
