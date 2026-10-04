#include "lsw/agent/install/process_runner.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <windows.h>

namespace lsw::agent::install {

ProcessOutcome ProcessRunner::run(const std::wstring& command_line, unsigned long timeout_ms) {
    ProcessOutcome outcome;

    const win::UniqueHandle job{::CreateJobObjectW(nullptr, nullptr)};

    win::SpawnOptions options;
    options.command_line = command_line;
    // CREATE_SUSPENDED so the process joins the job BEFORE it can spawn
    // descendants — otherwise an early grandchild escapes the job and
    // survives a timeout kill.
    options.creation_flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    options.hide_window = true;
    DWORD error = 0;
    const auto spawned = win::Process::spawn(options, &error);
    if (!spawned) {
        outcome.error = "CreateProcessW failed err=" + win::ErrorText::win32(error);
        return outcome;
    }
    const HANDLE process = spawned->process.get();
    if (job) {
        ::AssignProcessToJobObject(job.get(), process);
    }
    ::ResumeThread(spawned->thread.get());

    LSW_AGENT_INFO("process: spawned pid={} timeout={} cmd={}", spawned->pid,
                   timeout_ms == kNoTimeout ? std::string{"none"}
                                            : std::to_string(timeout_ms / 1000) + "s",
                   command_line);

    outcome.completed = true;
    if (::WaitForSingleObject(process, timeout_ms) == WAIT_TIMEOUT) {
        LSW_AGENT_ERROR("process: pid={} TIMED OUT after {}s — killing process tree", spawned->pid,
                        timeout_ms / 1000);
        if (job) {
            ::TerminateJobObject(job.get(), 1);
        } else {
            ::TerminateProcess(process, 1);
        }
        ::WaitForSingleObject(process, 5000);
        outcome.error =
            "timed out after " + std::to_string(timeout_ms / 1000) + "s (process tree killed)";
        outcome.completed = false;
    }

    DWORD code = 0;
    ::GetExitCodeProcess(process, &code);
    if (outcome.completed) {
        LSW_AGENT_INFO("process: pid={} exited code={}", spawned->pid, code);
    }
    outcome.exit_code = static_cast<int>(code);
    return outcome;
}

}  // namespace lsw::agent::install
