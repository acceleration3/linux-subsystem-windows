#include "lsw/agent/core/agent_service.hpp"
#include "lsw/agent/core/command_line.hpp"
#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/core/service_host.hpp"
#include "lsw/agent/core/startup_header.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/session/session_host.hpp"
#include "lsw/agent/win/rolling_file.hpp"
#include "lsw/agent/win/wts_sessions.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdlib>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    using lsw::agent::core::CommandLine;

    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    const CommandLine cl = CommandLine::parse(args);

    std::string role = "foreground";
    if (cl.mode == CommandLine::Mode::SessionHelper) {
        const auto session = lsw::agent::win::WtsSessions::current();
        lsw::agent::log::set_file(lsw::agent::core::Paths::session_helper_log(session),
                                  lsw::agent::win::RollingFile::kDefaultLimits);
        lsw::agent::log::set_source("agent session " + std::to_string(session));
        role = "session helper in session " + std::to_string(session);
    } else if (cl.mode == CommandLine::Mode::Service) {
        role = "service";
    }
    lsw::agent::core::StartupHeader::write(role);
    if (!cl.error.empty()) {
        LSW_AGENT_ERROR("agent: {}", cl.error);
        lsw::agent::log::flush(1000);
        return 2;
    }

    switch (cl.mode) {
    case CommandLine::Mode::Service:
        return lsw::agent::core::ServiceHost::run();
    case CommandLine::Mode::SessionHelper: {
        const int code = lsw::agent::session::SessionHost::run(cl.pipe);
        LSW_AGENT_INFO("helper: exiting (code {})", code);
        lsw::agent::log::flush(1000);
        ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(code));
        return code;
    }
    case CommandLine::Mode::Foreground:
        break;
    }
    const int code = lsw::agent::core::AgentService::instance().run(
        lsw::agent::core::AgentService::Mode::Foreground);
    lsw::agent::log::flush(1000);
    return code;
}
