#include "lsw/agent/session/input_relay_supervisor.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/file_system.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/rolling_file.hpp"
#include "lsw/agent/win/run_as.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace lsw::agent::session {

namespace {
constexpr auto kMaxBackoff = std::chrono::seconds(60);
constexpr auto kHealthyAfter = std::chrono::seconds(60);
constexpr DWORD kExitGraceMs = 2000;

std::string log_name(std::uint32_t session) {
    return "input-relay-" + std::to_string(session);
}

}

InputRelaySupervisor::~InputRelaySupervisor() {
    stop_all();
}

bool InputRelaySupervisor::exited(const Relay& relay, std::uint32_t session) {
    if (::WaitForSingleObject(relay.process.get(), 0) != WAIT_OBJECT_0) {
        return false;
    }
    DWORD code = 0;
    ::GetExitCodeProcess(relay.process.get(), &code);
    LSW_AGENT_WARN("sessions: the input relay in session {} (pid {}) exited with code {}", session,
                   relay.pid, code);
    return true;
}

bool InputRelaySupervisor::launch(std::uint32_t session, Relay& relay) {
    const std::wstring exe = core::Paths::remotewin_guest_exe();
    if (!win::FileSystem::exists(exe)) {
        LSW_AGENT_WARN("sessions: no input relay for session {}: {} does not exist", session,
                       util::TextEncoding::to_utf8(exe));
        return false;
    }
    const std::wstring log_file = core::Paths::remotewin_guest_log(log_name(session));
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path{log_file}.parent_path(), ignored);
    win::RollingFile{log_file}.roll_now();

    win::SpawnOptions options;
    options.application = exe;
    options.command_line = win::Process::quote(exe) + L" --input-relay --session " +
                           std::to_wstring(session);
    options.creation_flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
    options.working_dir = core::Paths::root();
    std::string why;
    DWORD spawn_error = 0;
    std::optional<win::SpawnedProcess> spawned;
    try {
        const win::RunAs as = win::RunAs::system_in_session(session);
        options.environment = as.environment({{"RWIN_LOG_FILE", util::TextEncoding::to_utf8(log_file)}});
        as.apply(options);
        spawned = win::Process::spawn(options, &spawn_error);
        if (spawned) {
            LSW_AGENT_INFO("sessions: started the input relay pid={} in session {} as {}",
                           spawned->pid, session, as.describe());
        }
    } catch (const std::exception& e) {
        why = e.what();
    }
    if (!spawned) {
        LSW_AGENT_WARN("sessions: cannot start the input relay in session {}: {}", session,
                       why.empty() ? win::ErrorText::win32(spawn_error) : why);
        return false;
    }
    relay.process = std::move(spawned->process);
    relay.pid = spawned->pid;
    relay.started = std::chrono::steady_clock::now();
    return true;
}

void InputRelaySupervisor::reconcile(const std::vector<std::uint32_t>& sessions) {
    for (auto it = relays_.begin(); it != relays_.end();) {
        if (std::find(sessions.begin(), sessions.end(), it->first) == sessions.end()) {
            LSW_AGENT_INFO("sessions: session {} is gone; stopping its input relay", it->first);
            if (it->second.process) {
                ::TerminateProcess(it->second.process.get(), 0);
                ::WaitForSingleObject(it->second.process.get(), kExitGraceMs);
            }
            it = relays_.erase(it);
            continue;
        }
        ++it;
    }
    const auto now = std::chrono::steady_clock::now();
    for (const std::uint32_t session : sessions) {
        Relay& relay = relays_[session];
        if (relay.process) {
            if (!exited(relay, session)) {
                continue;
            }
            const bool healthy = now - relay.started >= kHealthyAfter;
            relay.backoff = healthy ? std::chrono::seconds(1)
                                    : std::min<std::chrono::seconds>(relay.backoff * 2, kMaxBackoff);
            relay.retry_at = now + relay.backoff;
            relay.process.reset();
            LSW_AGENT_WARN("sessions: restarting the input relay in session {} in {}s", session,
                           relay.backoff.count());
        }
        if (now < relay.retry_at) {
            continue;
        }
        if (!launch(session, relay)) {
            relay.backoff = std::min<std::chrono::seconds>(relay.backoff * 2, kMaxBackoff);
            relay.retry_at = now + relay.backoff;
        }
    }
}

void InputRelaySupervisor::stop_all() {
    for (auto& [session, relay] : relays_) {
        if (!relay.process) {
            continue;
        }
        LSW_AGENT_INFO("sessions: stopping the input relay in session {} (pid {})", session,
                       relay.pid);
        ::TerminateProcess(relay.process.get(), 0);
        ::WaitForSingleObject(relay.process.get(), kExitGraceMs);
    }
    relays_.clear();
}

}
