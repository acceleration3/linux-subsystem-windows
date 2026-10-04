#include "lsw/agent/core/service_host.hpp"

#include "lsw/agent/core/agent_service.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <wtsapi32.h>
// clang-format on

#include <mutex>

namespace lsw::agent::core {

namespace {

// How long the SCM is told a start or a stop may take. A stop ends the
// connections (one tick each, or a forwarded request's 8 s at worst) and
// the session helpers (2 s grace each, then terminated).
constexpr DWORD kStartHintMs = 10000;
constexpr DWORD kStopHintMs = 30000;

// The service's status, shared by the service thread and the control
// handler (which the SCM calls on the dispatcher thread).
struct Status {
    std::mutex mu;
    SERVICE_STATUS_HANDLE handle = nullptr;
    SERVICE_STATUS status{};

    void report(DWORD state, DWORD wait_hint = 0, DWORD exit_code = NO_ERROR, DWORD specific = 0) {
        const std::lock_guard lock(mu);
        if (handle == nullptr) {
            return;
        }
        status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        status.dwCurrentState = state;
        status.dwWaitHint = wait_hint;
        status.dwWin32ExitCode = exit_code;
        status.dwServiceSpecificExitCode = specific;
        // Controls are only accepted while running: a STOP during
        // START_PENDING has nothing to stop yet.
        status.dwControlsAccepted =
            state == SERVICE_RUNNING
                ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE
                : 0;
        // Pending states count checkpoints up; settled ones reset them.
        status.dwCheckPoint =
            (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : status.dwCheckPoint + 1;
        if (::SetServiceStatus(handle, &status) == FALSE) {
            LSW_AGENT_WARN("service: SetServiceStatus({}) failed err={}", state,
                           win::ErrorText::last_error());
        }
    }

    [[nodiscard]] DWORD state() {
        const std::lock_guard lock(mu);
        return status.dwCurrentState;
    }
};

Status& status() {
    static auto* s = new Status;  // never destroyed: the SCM may call late
    return *s;
}

DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID event_data,
                             LPVOID /*context*/) {
    switch (control) {
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        LSW_AGENT_INFO("service: {} requested",
                       control == SERVICE_CONTROL_STOP ? "stop" : "shutdown");
        if (status().state() == SERVICE_RUNNING) {
            status().report(SERVICE_STOP_PENDING, kStopHintMs);
        }
        AgentService::instance().request_stop();
        return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE: {
        // Logon, logoff, console connect...: the session helpers follow.
        const auto* note = static_cast<const WTSSESSION_NOTIFICATION*>(event_data);
        if (note != nullptr) {
            AgentService::instance().on_session_change(note->dwSessionId, event_type);
        }
        return NO_ERROR;
    }
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI service_main(DWORD /*argc*/, LPWSTR* /*argv*/) {
    Status& st = status();
    {
        const std::lock_guard lock(st.mu);
        st.handle = ::RegisterServiceCtrlHandlerExW(ServiceHost::kName, &control_handler, nullptr);
    }
    if (st.handle == nullptr) {
        LSW_AGENT_ERROR("service: RegisterServiceCtrlHandlerEx failed err={}",
                        win::ErrorText::last_error());
        return;
    }
    st.report(SERVICE_START_PENDING, kStartHintMs);
    // Everything slow — the desktop gate above all — happens inside run,
    // after this: RUNNING is "the agent is up", not "the port is open".
    st.report(SERVICE_RUNNING);
    LSW_AGENT_INFO("service: running");

    const int code = AgentService::instance().run(AgentService::Mode::Service);

    LSW_AGENT_INFO("service: stopped (exit code {})", code);
    log::flush(1000);
    if (code == 0) {
        st.report(SERVICE_STOPPED);
    } else {
        st.report(SERVICE_STOPPED, 0, ERROR_SERVICE_SPECIFIC_ERROR, static_cast<DWORD>(code));
    }
}

}  // namespace

int ServiceHost::run() {
    // The name is ignored for an own-process service, but must be there.
    wchar_t name[] = L"lsw-agent";
    const SERVICE_TABLE_ENTRYW table[] = {
        {name, &service_main},
        {nullptr, nullptr},
    };
    if (::StartServiceCtrlDispatcherW(table) == FALSE) {
        const DWORD err = ::GetLastError();
        LSW_AGENT_ERROR("service: StartServiceCtrlDispatcher failed err={}{}",
                        win::ErrorText::win32(err),
                        err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT
                            ? " (--service is for the SCM; run without arguments to debug)"
                            : "");
        log::flush(1000);
        return 1;
    }
    return 0;
}

}  // namespace lsw::agent::core
