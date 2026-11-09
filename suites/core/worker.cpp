#include "core/worker.hpp"

#include "core/child_mode.hpp"
#include "core/child_protocol.hpp"
#include "core/text.hpp"

#include <windows.h>

#include <chrono>
#include <cwchar>
#include <system_error>
#include <vector>

namespace lsw::suites {

namespace {

constexpr std::size_t kOutputCap = 4u << 20;

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

    [[nodiscard]] HANDLE get() const { return handle_; }

private:
    HANDLE handle_ = nullptr;
};

std::wstring child_environment(const std::vector<std::pair<std::wstring, std::wstring>>& extra) {
    std::wstring block;
    wchar_t* strings = ::GetEnvironmentStringsW();
    if (strings != nullptr) {
        const wchar_t* at = strings;
        while (*at != L'\0') {
            const std::size_t length = std::wcslen(at) + 1;
            block.append(at, length);
            at += length;
        }
        ::FreeEnvironmentStringsW(strings);
    }
    block += std::wstring(ChildMode::kVariable) + L"=1";
    block.push_back(L'\0');
    for (const auto& [name, value] : extra) {
        block += name + L"=" + value;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

HANDLE restricted_primary_token() {
    HANDLE own = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
                           &own) == FALSE) {
        return nullptr;
    }
    HANDLE restricted = nullptr;
    if (::CreateRestrictedToken(own, LUA_TOKEN, 0, nullptr, 0, nullptr, 0, nullptr,
                                &restricted) == FALSE) {
        restricted = nullptr;
    }
    ::CloseHandle(own);
    return restricted;
}

std::wstring quote(const std::wstring& text) {
    return L"\"" + text + L"\"";
}

void drain(HANDLE pipe, std::string& output) {
    for (;;) {
        DWORD available = 0;
        if (::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE ||
            available == 0) {
            return;
        }
        char buffer[8192];
        DWORD got = 0;
        if (::ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) == FALSE || got == 0) {
            return;
        }
        if (output.size() < kOutputCap) {
            output.append(buffer, got);
        }
    }
}

std::string describe_exit(DWORD code) {
    if ((code & 0xC0000000u) == 0xC0000000u) {
        return "exception 0x" + Text::hex(code, 8);
    }
    return "exit code " + std::to_string(code) + " without a result";
}

}  // namespace

Worker::Worker(std::filesystem::path artifacts) : artifacts_(std::move(artifacts)) {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
    exe_.assign(buffer, length);
}

CheckResult Worker::run(const std::string& id, int timeout_seconds) const {
    CheckResult result;
    result.id = id;
    const auto begin = std::chrono::steady_clock::now();
    const auto finish = [&](Status status, const std::string& reason) {
        result.status = status;
        result.reason = reason;
        result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
        return result;
    };

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle read_end;
    Handle write_end;
    HANDLE pipe_read = nullptr;
    HANDLE pipe_write = nullptr;
    if (::CreatePipe(&pipe_read, &pipe_write, &inherit, 0) == FALSE) {
        return finish(Status::Crash, "cannot create the output pipe");
    }
    read_end.reset(pipe_read);
    write_end.reset(pipe_write);
    ::SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0);
    Handle nul(::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

    HANDLE inherited[2] = {write_end.get(), nul.get()};
    SIZE_T attribute_size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<char> attribute_storage(attribute_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (::InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) == FALSE ||
        ::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                    sizeof(inherited), nullptr, nullptr) == FALSE) {
        return finish(Status::Crash, "cannot restrict the inherited handles");
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul.get();
    startup.StartupInfo.hStdOutput = write_end.get();
    startup.StartupInfo.hStdError = write_end.get();
    startup.lpAttributeList = attributes;

    std::wstring command = quote(exe_) + L" --run-one " + Text::wide(id) + L" --artifacts " +
                           quote(std::filesystem::absolute(artifacts_).wstring());

    Handle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job.get() == nullptr ||
        ::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                  sizeof(limits)) == FALSE) {
        ::DeleteProcThreadAttributeList(attributes);
        return finish(Status::Crash, "cannot create the job object");
    }

    std::wstring environment;
    if (child_mode_) {
        environment = child_environment(env_);
    }
    Handle token;
    if (restricted_) {
        token.reset(restricted_primary_token());
        if (token.get() == nullptr) {
            ::DeleteProcThreadAttributeList(attributes);
            return finish(Status::Crash, "cannot create the restricted token");
        }
    }
    DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
    if (child_mode_) {
        flags |= CREATE_UNICODE_ENVIRONMENT;
    }
    PROCESS_INFORMATION info{};
    void* environment_block = child_mode_ ? static_cast<void*>(environment.data()) : nullptr;
    const wchar_t* directory = cwd_.empty() ? nullptr : cwd_.c_str();
    const BOOL created =
        restricted_ ? ::CreateProcessAsUserW(token.get(), exe_.c_str(), command.data(), nullptr,
                                             nullptr, TRUE, flags, environment_block, directory,
                                             &startup.StartupInfo, &info)
                    : ::CreateProcessW(exe_.c_str(), command.data(), nullptr, nullptr, TRUE, flags,
                                       environment_block, directory, &startup.StartupInfo, &info);
    ::DeleteProcThreadAttributeList(attributes);
    if (created == FALSE) {
        return finish(Status::Crash, "cannot start the worker (error " +
                                         std::to_string(::GetLastError()) + ")");
    }
    Handle process(info.hProcess);
    Handle thread(info.hThread);
    result.pid = info.dwProcessId;
    write_end.reset();
    nul.reset();
    if (::AssignProcessToJobObject(job.get(), process.get()) == FALSE) {
        ::TerminateProcess(process.get(), 1);
        return finish(Status::Crash, "cannot place the worker in its job");
    }
    ::ResumeThread(thread.get());

    auto deadline = timeout_ms_ > 0 ? begin + std::chrono::milliseconds(timeout_ms_) : begin + std::chrono::seconds(timeout_seconds);
    std::string output;
    bool timed_out = false;
    bool triggered = false;
    for (;;) {
        drain(read_end.get(), output);
        if (::WaitForSingleObject(process.get(), 20) == WAIT_OBJECT_0) {
            break;
        }
        if (trigger_ != nullptr && !triggered && ::WaitForSingleObject(trigger_, 0) == WAIT_OBJECT_0) {
            triggered = true;
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(trigger_ms_);
        }
        if (std::chrono::steady_clock::now() > deadline) {
            timed_out = true;
            ::TerminateJobObject(job.get(), 1);
            ::WaitForSingleObject(process.get(), 5000);
            break;
        }
    }
    ::TerminateJobObject(job.get(), 1);
    drain(read_end.get(), output);

    DWORD code = 0;
    ::GetExitCodeProcess(process.get(), &code);
    const bool have_status = ChildProtocol::decode(output, result);
    const std::filesystem::path dump = artifacts_ / id / "crash.dmp";
    std::error_code dump_error;
    if (std::filesystem::exists(dump, dump_error)) {
        result.artifacts.push_back(dump.string());
    }
    const std::string where = result.phase.empty() ? std::string() : " (running: " + result.phase + ")";
    if (timed_out) {
        if (triggered) {
            return finish(Status::Timeout, "terminated " + std::to_string(trigger_ms_) + " ms after the child reported ready");
        }
        return finish(Status::Timeout, timeout_ms_ > 0 ? "no result after " + std::to_string(timeout_ms_) + " ms" + where : "no result after " + std::to_string(timeout_seconds) + " s" + where);
    }
    if (!have_status) {
        return finish(Status::Crash, describe_exit(code) + where);
    }
    return finish(result.status, result.reason);
}

}  // namespace lsw::suites
