#pragma once

namespace lsw::agent::core {

// The Service Control Manager's side of ``lsw-agent.exe --service``.
//
// Registers as SERVICE_WIN32_OWN_PROCESS, reports RUNNING as soon as the
// agent has started (the install gate is the agent's own business and
// can take many minutes, far past what the SCM tolerates in
// START_PENDING), accepts STOP, SHUTDOWN and SESSIONCHANGE, and reports
// STOPPED with the agent's exit code — a non-zero one as a
// service-specific error, which the service's recovery actions (restart)
// act on.
class ServiceHost {
public:
    ServiceHost() = delete;

    // The name the service is registered under (autounattend.xml, the
    // redeploy script).
    static constexpr const wchar_t* kName = L"lsw-agent";

    // Hand the main thread to the SCM's dispatcher until the service
    // stops. Returns the process exit code; non-zero (with a log line)
    // when the process was not started by the SCM at all.
    [[nodiscard]] static int run();
};

}  // namespace lsw::agent::core
