#include "lsw/agent/console/console.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lsw::agent::console {

struct Session {
    HANDLE process = nullptr;
    HANDLE stdin_w = nullptr;
    HANDLE stdout_r = nullptr;
    std::thread reader;
    std::atomic<bool> alive{true};
};

struct Console::Impl {
    std::mutex mu;
    std::uint64_t next_id = 1;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
    std::vector<Console::Note> pending;  // drained by collect_pending
};

namespace {

// Find a usable cmd.exe path. %COMSPEC% is the canonical env var;
// fall back to the well-known location.
std::wstring resolve_cmd_path() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetEnvironmentVariableW(L"COMSPEC", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return std::wstring{buf};
    }
    return L"C:\\Windows\\System32\\cmd.exe";
}

}  // namespace

Console::Console() : impl_(new Impl) {}

Console::~Console() {
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        for (auto& kv : impl_->sessions) {
            auto& s = kv.second;
            s->alive.store(false);
            if (s->process != nullptr) {
                ::TerminateProcess(s->process, 1);
            }
            if (s->stdin_w != nullptr) ::CloseHandle(s->stdin_w);
            if (s->stdout_r != nullptr) ::CloseHandle(s->stdout_r);
        }
    }
    // Threads detect alive=false on next ReadFile completion. We
    // don't join them here — they're detached lambdas that clean
    // themselves up. The process handle is already closed by
    // TerminateProcess + the reader's CloseHandle.
    delete impl_;
}

std::string Console::open() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdin_r = nullptr;
    HANDLE stdin_w = nullptr;
    HANDLE stdout_r = nullptr;
    HANDLE stdout_w = nullptr;
    if (!::CreatePipe(&stdin_r, &stdin_w, &sa, 0)) {
        LSW_AGENT_ERROR(std::string{"console: stdin CreatePipe failed err="}
                        + std::to_string(::GetLastError()));
        return {};
    }
    if (!::CreatePipe(&stdout_r, &stdout_w, &sa, 0)) {
        LSW_AGENT_ERROR(std::string{"console: stdout CreatePipe failed err="}
                        + std::to_string(::GetLastError()));
        ::CloseHandle(stdin_r);
        ::CloseHandle(stdin_w);
        return {};
    }
    // The child inherits the read end of stdin and the write end of
    // stdout; we keep the opposite ends to talk to it.
    ::SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_r;
    si.hStdOutput = stdout_w;
    si.hStdError = stdout_w;

    std::wstring cmd_path = resolve_cmd_path();
    // CreateProcessW writes through the cmdline buffer.
    std::wstring cmdline = L"\"" + cmd_path + L"\" /Q";
    std::vector<wchar_t> mut(cmdline.begin(), cmdline.end());
    mut.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW so cmd.exe doesn't briefly flash a console
    // window. The child runs without a console of its own — fine for
    // pipe I/O.
    const BOOL ok = ::CreateProcessW(cmd_path.c_str(), mut.data(), nullptr, nullptr,
                                       TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                       &si, &pi);
    // Whether spawn succeeded or not, we no longer need the child's
    // ends locally.
    ::CloseHandle(stdin_r);
    ::CloseHandle(stdout_w);
    if (!ok) {
        LSW_AGENT_ERROR(std::string{"console: CreateProcess(cmd.exe) failed err="}
                        + std::to_string(::GetLastError()));
        ::CloseHandle(stdin_w);
        ::CloseHandle(stdout_r);
        return {};
    }
    ::CloseHandle(pi.hThread);

    auto sess = std::make_shared<Session>();
    sess->process = pi.hProcess;
    sess->stdin_w = stdin_w;
    sess->stdout_r = stdout_r;

    std::string sid;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        sid = "c" + std::to_string(impl_->next_id++);
        impl_->sessions[sid] = sess;
    }

    // Reader thread: drains stdout_r until EOF (cmd.exe exits) or
    // alive flips. Output goes into the pending queue, drained by
    // collect_pending and pushed over the wire as console_output
    // events.
    Impl* impl = impl_;
    sess->reader = std::thread{[impl, sid, sess]() {
        std::uint8_t buf[4096];
        for (;;) {
            DWORD got = 0;
            const BOOL r = ::ReadFile(sess->stdout_r, buf, sizeof(buf), &got, nullptr);
            if (!r || got == 0) {
                break;  // EOF (child exited) or pipe broken
            }
            std::vector<std::uint8_t> data(buf, buf + got);
            std::lock_guard<std::mutex> lock(impl->mu);
            impl->pending.push_back({Console::Kind::Output, sid, std::move(data), 0});
        }
        // Process exit code + final "Closed" notification.
        DWORD exit_code = 0;
        if (sess->process != nullptr) {
            ::WaitForSingleObject(sess->process, 5000);
            ::GetExitCodeProcess(sess->process, &exit_code);
        }
        std::lock_guard<std::mutex> lock(impl->mu);
        Console::Note done;
        done.kind = Console::Kind::Closed;
        done.session_id = sid;
        done.exit_code = static_cast<int>(exit_code);
        impl->pending.push_back(std::move(done));
        sess->alive.store(false);
    }};
    sess->reader.detach();

    LSW_AGENT_INFO(std::string{"console: session "} + sid + " opened pid="
                   + std::to_string(pi.dwProcessId));
    return sid;
}

bool Console::write(const std::string& sid, const std::string& data) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        const auto it = impl_->sessions.find(sid);
        if (it == impl_->sessions.end()) return false;
        sess = it->second;
    }
    if (!sess->alive.load()) return false;
    DWORD wrote = 0;
    if (!::WriteFile(sess->stdin_w, data.data(),
                      static_cast<DWORD>(data.size()), &wrote, nullptr)) {
        LSW_AGENT_WARN(std::string{"console: write to "} + sid
                       + " failed err=" + std::to_string(::GetLastError()));
        return false;
    }
    return true;
}

void Console::close(const std::string& sid) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        const auto it = impl_->sessions.find(sid);
        if (it == impl_->sessions.end()) return;
        sess = it->second;
        impl_->sessions.erase(it);
    }
    sess->alive.store(false);
    if (sess->stdin_w != nullptr) {
        // Closing stdin_w lets cmd.exe see EOF on stdin and exit
        // naturally. If it doesn't (e.g. mid-command), the
        // TerminateProcess below ensures cleanup.
        ::CloseHandle(sess->stdin_w);
        sess->stdin_w = nullptr;
    }
    if (sess->process != nullptr) {
        // 100 ms grace, then force-kill. cmd.exe responds to stdin
        // EOF quickly so this rarely fires.
        if (::WaitForSingleObject(sess->process, 100) == WAIT_TIMEOUT) {
            ::TerminateProcess(sess->process, 1);
        }
    }
    LSW_AGENT_INFO(std::string{"console: session "} + sid + " closed");
}

std::vector<Console::Note> Console::collect_pending() {
    std::vector<Note> out;
    std::lock_guard<std::mutex> lock(impl_->mu);
    out.swap(impl_->pending);
    return out;
}

}  // namespace lsw::agent::console
