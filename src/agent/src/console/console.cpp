#include "lsw/agent/console/console.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/agent/win/unique_handle.hpp"

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
#include <utility>
#include <vector>

namespace lsw::agent::console {

// One cmd.exe. Shared between the session map and the session's reader
// thread, so whichever lets go last closes the handles — the reader is
// still inside ReadFile on ``stdout_r`` when ``close`` erases the map
// entry, and must not have it closed underneath it.
struct Session {
    win::UniqueHandle process;
    win::UniqueHandle stdin_w;
    win::UniqueHandle stdout_r;  // read only by the reader thread
    std::atomic<bool> alive{true};
};

// Shared with every reader thread, which are detached: a reader can
// still be pushing its last notes after ~Console, so the state it pushes
// into has to outlive the Console that created it.
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

Console::Console() : impl_(std::make_shared<Impl>()) {}

Console::~Console() {
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
    {
        const std::lock_guard<std::mutex> lock(impl_->mu);
        sessions.swap(impl_->sessions);
    }
    // Killing the process breaks its end of the stdout pipe, so each
    // reader's ReadFile returns, it posts its Closed note into the Impl
    // it co-owns, and the last reference to the session closes the
    // handles. Nothing to join: the readers are detached and need
    // nothing from this object.
    for (auto& [sid, sess] : sessions) {
        sess->alive.store(false);
        ::TerminateProcess(sess->process.get(), 1);
        sess->stdin_w.reset();
    }
}

std::string Console::open(const win::RunAs& as) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    win::UniqueHandle stdin_r;
    win::UniqueHandle stdin_w;
    win::UniqueHandle stdout_r;
    win::UniqueHandle stdout_w;
    if (!::CreatePipe(stdin_r.put(), stdin_w.put(), &sa, 0)) {
        LSW_AGENT_ERROR("console: stdin CreatePipe failed err={}", win::ErrorText::last_error());
        return {};
    }
    if (!::CreatePipe(stdout_r.put(), stdout_w.put(), &sa, 0)) {
        LSW_AGENT_ERROR("console: stdout CreatePipe failed err={}", win::ErrorText::last_error());
        return {};
    }
    // The child inherits the read end of stdin and the write end of
    // stdout; we keep the opposite ends to talk to it.
    ::SetHandleInformation(stdin_w.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(stdout_r.get(), HANDLE_FLAG_INHERIT, 0);

    win::SpawnOptions options;
    options.application = resolve_cmd_path();
    options.command_line = win::Process::quote(options.application) + L" /Q";
    // CREATE_NO_WINDOW so cmd.exe gets a console nobody sees instead of
    // a window on the user's desktop. Its I/O is the pipes.
    options.creation_flags = CREATE_NO_WINDOW;
    options.inherit_handles = true;
    options.std_input = stdin_r.get();
    options.std_output = stdout_w.get();
    options.std_error = stdout_w.get();
    try {
        as.apply(options);
    } catch (const std::exception& e) {
        LSW_AGENT_ERROR("console: cannot start cmd.exe as {}: {}", as.describe(), e.what());
        return {};
    }
    DWORD error = 0;
    auto spawned = win::Process::spawn(options, &error);
    if (!spawned) {
        LSW_AGENT_ERROR("console: CreateProcess(cmd.exe) as {} failed err={}", as.describe(),
                        win::ErrorText::win32(error));
        return {};
    }
    // The child has its own copies of these now. Ours must go, or the
    // stdout pipe never reports EOF when the child exits — this process
    // would still hold a write end.
    stdin_r.reset();
    stdout_w.reset();

    auto sess = std::make_shared<Session>();
    sess->process = std::move(spawned->process);
    sess->stdin_w = std::move(stdin_w);
    sess->stdout_r = std::move(stdout_r);

    std::string sid;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        sid = "c" + std::to_string(impl_->next_id++);
        impl_->sessions[sid] = sess;
    }

    // Reader thread: drains stdout_r until EOF (cmd.exe exits) or the
    // pipe breaks. Output goes into the pending queue, drained by
    // collect_pending and pushed over the wire as console_output
    // events.
    std::thread{[impl = impl_, sid, sess]() {
        std::uint8_t buf[4096];
        for (;;) {
            DWORD got = 0;
            const BOOL r = ::ReadFile(sess->stdout_r.get(), buf, sizeof(buf), &got, nullptr);
            if (!r || got == 0) {
                break;  // EOF (child exited) or pipe broken
            }
            std::vector<std::uint8_t> data(buf, buf + got);
            std::lock_guard<std::mutex> lock(impl->mu);
            impl->pending.push_back({Console::Kind::Output, sid, std::move(data), 0});
        }
        // Process exit code + final "Closed" notification.
        DWORD exit_code = 0;
        ::WaitForSingleObject(sess->process.get(), 5000);
        ::GetExitCodeProcess(sess->process.get(), &exit_code);
        std::lock_guard<std::mutex> lock(impl->mu);
        Console::Note done;
        done.kind = Console::Kind::Closed;
        done.session_id = sid;
        done.exit_code = static_cast<int>(exit_code);
        impl->pending.push_back(std::move(done));
        sess->alive.store(false);
    }}.detach();

    LSW_AGENT_INFO("console: session {} opened pid={} as {}", sid, spawned->pid, as.describe());
    return sid;
}

bool Console::write(const std::string& sid, const std::string& data) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        const auto it = impl_->sessions.find(sid);
        if (it == impl_->sessions.end())
            return false;
        sess = it->second;
    }
    if (!sess->alive.load())
        return false;
    DWORD wrote = 0;
    if (!::WriteFile(sess->stdin_w.get(), data.data(), static_cast<DWORD>(data.size()), &wrote,
                     nullptr)) {
        LSW_AGENT_WARN("console: write to {} failed err={}", sid, win::ErrorText::last_error());
        return false;
    }
    return true;
}

void Console::close(const std::string& sid) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        const auto it = impl_->sessions.find(sid);
        if (it == impl_->sessions.end())
            return;
        sess = it->second;
        impl_->sessions.erase(it);
    }
    sess->alive.store(false);
    // Closing stdin_w lets cmd.exe see EOF on stdin and exit naturally.
    // If it doesn't (e.g. mid-command), the TerminateProcess below
    // ensures cleanup. The process and stdout handles stay with the
    // reader thread, which closes them once the pipe reports the exit.
    sess->stdin_w.reset();
    // 100 ms grace, then force-kill. cmd.exe responds to stdin EOF
    // quickly so this rarely fires.
    if (::WaitForSingleObject(sess->process.get(), 100) == WAIT_TIMEOUT) {
        ::TerminateProcess(sess->process.get(), 1);
    }
    LSW_AGENT_INFO("console: session {} closed", sid);
}

std::vector<Console::Note> Console::collect_pending() {
    std::vector<Note> out;
    std::lock_guard<std::mutex> lock(impl_->mu);
    out.swap(impl_->pending);
    return out;
}

}  // namespace lsw::agent::console
