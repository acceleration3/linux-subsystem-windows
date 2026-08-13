#include "lsw/agent/install/process_runner.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

#include <vector>

namespace lsw::agent::install {

ProcessOutcome ProcessRunner::run(const std::wstring& command_line,
                                  unsigned long timeout_ms) {
    ProcessOutcome outcome;

    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    // CreateProcessW writes into the command-line buffer, so it must be
    // mutable.
    std::vector<wchar_t> mutable_cmd(command_line.begin(), command_line.end());
    mutable_cmd.push_back(L'\0');

    // CREATE_SUSPENDED so the process joins the job BEFORE it can spawn
    // descendants — otherwise an early grandchild escapes the job and
    // survives a timeout kill.
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                          &si, &pi)) {
        outcome.error = std::string{"CreateProcessW failed err="}
                        + std::to_string(::GetLastError());
        if (job != nullptr) {
            ::CloseHandle(job);
        }
        return outcome;
    }
    if (job != nullptr) {
        ::AssignProcessToJobObject(job, pi.hProcess);
    }
    ::ResumeThread(pi.hThread);

    LSW_AGENT_INFO(std::string{"process: spawned pid="} + std::to_string(pi.dwProcessId)
                   + " timeout="
                   + (timeout_ms == kNoTimeout ? std::string{"none"}
                                               : std::to_string(timeout_ms / 1000) + "s")
                   + " cmd=" + util::TextEncoding::to_utf8(command_line));

    outcome.completed = true;
    if (::WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        LSW_AGENT_ERROR(std::string{"process: pid="} + std::to_string(pi.dwProcessId)
                        + " TIMED OUT after " + std::to_string(timeout_ms / 1000)
                        + "s — killing process tree");
        if (job != nullptr) {
            ::TerminateJobObject(job, 1);
        } else {
            ::TerminateProcess(pi.hProcess, 1);
        }
        ::WaitForSingleObject(pi.hProcess, 5000);
        outcome.error = "timed out after " + std::to_string(timeout_ms / 1000)
                        + "s (process tree killed)";
        outcome.completed = false;
    }

    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    if (outcome.completed) {
        LSW_AGENT_INFO(std::string{"process: pid="} + std::to_string(pi.dwProcessId)
                       + " exited code=" + std::to_string(code));
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    if (job != nullptr) {
        ::CloseHandle(job);
    }
    outcome.exit_code = static_cast<int>(code);
    return outcome;
}

}  // namespace lsw::agent::install
