#include "lsw/agent/session/session_host.hpp"

#include "lsw/agent/core/services.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/context.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/session/helper_protocol.hpp"
#include "lsw/agent/session/pipe_codec.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/agent/win/desktop_probe.hpp"
#include "lsw/agent/win/desktop_session.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/overlapped_pipe.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/wts_sessions.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lsw::agent::session {

namespace {

// Requests are served this many at a time, so the window watcher's
// list_windows polls are not stuck behind one slow UI Automation call.
constexpr int kWorkers = 4;

// How often UI Automation events and the helper's log are pushed up.
constexpr auto kPumpTick = std::chrono::milliseconds(16);

// How long to keep trying the service's pipe. The service creates it
// before starting the helper, so this only covers a busy instance.
constexpr auto kDialFor = std::chrono::seconds(30);

// The process that started this one, from a process snapshot.
DWORD parent_pid() {
    const DWORD self = ::GetCurrentProcessId();
    for (const auto& e : win::ProcessInfo::snapshot()) {
        if (e.pid == self) {
            return e.parent;
        }
    }
    return 0;
}

// Dial the service's pipe. Identification-level impersonation only: the
// service has no business acting as this user through the pipe.
HANDLE dial(const std::wstring& name) {
    const auto deadline = std::chrono::steady_clock::now() + kDialFor;
    for (;;) {
        HANDLE h = ::CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }
        const DWORD err = ::GetLastError();
        if (std::chrono::steady_clock::now() >= deadline ||
            (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND)) {
            LSW_AGENT_ERROR("helper: cannot open {} err={}", name, win::ErrorText::win32(err));
            return INVALID_HANDLE_VALUE;
        }
        if (err == ERROR_PIPE_BUSY) {
            ::WaitNamedPipeW(name.c_str(), 1000);
        } else {
            ::Sleep(200);
        }
    }
}

// Everything the helper's threads share. Heap-allocated and never freed:
// the desktop gate's thread can still be inside a 30-minute wait when the
// process ends, and must not find this gone.
struct Shared {
    explicit Shared(HANDLE handle)
        : pipe(handle), stop(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    win::OverlappedPipe pipe;
    win::UniqueHandle stop;

    // Send one message to the service; a failed write ends the helper.
    void send(const nlohmann::json& message) {
        const std::string bytes = PipeCodec::encode(message);
        if (!pipe.write_all(
                bytes.data(), bytes.size(),
                std::chrono::duration_cast<std::chrono::milliseconds>(kPipeWriteTimeout))) {
            ::SetEvent(stop.get());
        }
    }

    [[nodiscard]] bool stopping() const {
        return ::WaitForSingleObject(stop.get(), 0) == WAIT_OBJECT_0;
    }

    std::mutex mu;
    std::condition_variable cv;
    std::deque<nlohmann::json> requests;
};

// Serve one request with this process's handler for it.
nlohmann::json serve(const nlohmann::json& request, rpc::RpcContext& ctx) {
    const auto id = request.value("id", std::uint64_t{0});
    const auto cmd = request.value("cmd", std::string{});
    const auto params = (request.contains("params") && request["params"].is_object())
                            ? request["params"]
                            : nlohmann::json::object();
    nlohmann::json reply = {{"id", id}};
    auto* handler = rpc::RpcRegistry::instance().find(cmd);
    if (handler == nullptr) {
        reply["error"] = "unknown command: " + cmd;
    } else if (handler->placement() != rpc::RpcHandler::Placement::UserSession) {
        reply["error"] = cmd + ": not served by the session helper";
    } else {
        try {
            reply["result"] = handler->handle(params, ctx);
        } catch (const std::exception& e) {
            reply["error"] = e.what();
            LSW_AGENT_WARN("helper: cmd={} threw: {}", cmd, e.what());
        }
    }
    return reply;
}

// The desktop gate, as the agent ran it when it was itself started at
// logon: wait for the desktop to be genuinely usable (not just for the
// shell to exist under the first-logon overlay), then tell the service.
void run_gate(Shared& shared) {
    const auto started = std::chrono::steady_clock::now();
    // The desktop must stay uncovered for a couple of seconds, not just
    // be uncovered at one instant: OOBE hands off between several
    // full-screen hosts and there are gaps in between that would
    // otherwise read as ready.
    const bool ready = win::DesktopSession::wait_for_desktop(
        std::chrono::minutes(30), std::chrono::milliseconds(500), std::chrono::seconds(3),
        std::chrono::seconds(90));
    if (!ready) {
        LSW_AGENT_WARN("helper: desktop never became usable after 30 min");
    }
    // Stamp the exact state at the instant the gate decided, so it can be
    // lined up against the probe trace either side of it.
    win::DesktopProbe::write(win::DesktopProbe::snapshot("gate-released"));
    const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started);
    shared.send({{"event", helper_topic::kDesktop},
                 {"ready", ready},
                 {"waited_s", static_cast<std::int64_t>(waited.count())}});
}

// Push UI Automation events and this process's log records up, until the
// helper stops.
void run_pump(Shared& shared, uia::Uia& uia) {
    std::uint64_t log_cursor = 0;
    while (!shared.stopping()) {
        for (const auto& record : log::collect_since(log_cursor, log_cursor)) {
            shared.send({{"event", helper_topic::kLog},
                         {"level", log::level_name(record.level)},
                         {"msg", record.msg}});
        }
        for (auto& ev : uia.collect_pending()) {
            nlohmann::json message = std::move(ev.payload);
            message["event"] = ev.topic;
            shared.send(message);
        }
        ::WaitForSingleObject(shared.stop.get(), static_cast<DWORD>(kPumpTick.count()));
    }
}

void run_worker(Shared& shared, rpc::RpcContext& ctx) {
    for (;;) {
        nlohmann::json request;
        {
            std::unique_lock lock(shared.mu);
            shared.cv.wait_for(lock, std::chrono::milliseconds(250),
                               [&] { return !shared.requests.empty(); });
            if (shared.requests.empty()) {
                if (shared.stopping()) {
                    return;
                }
                continue;
            }
            request = std::move(shared.requests.front());
            shared.requests.pop_front();
        }
        shared.send(serve(request, ctx));
    }
}

}  // namespace

int SessionHost::run(const std::wstring& pipe_name) {
    const std::uint32_t session = win::WtsSessions::current();
    LSW_AGENT_INFO("helper: start in session {} (pid {})", session, ::GetCurrentProcessId());

    const HANDLE handle = dial(pipe_name);
    if (handle == INVALID_HANDLE_VALUE) {
        return 2;
    }
    // The pipe must be the one our own parent — the service that started
    // us — is serving. A name squatted by anything else is refused.
    ULONG server_pid = 0;
    const DWORD parent = parent_pid();
    if (::GetNamedPipeServerProcessId(handle, &server_pid) == FALSE || server_pid != parent) {
        LSW_AGENT_ERROR("helper: {} is served by pid {}, not by the agent that started this "
                        "helper (pid {}); refusing it",
                        pipe_name, server_pid, parent);
        ::CloseHandle(handle);
        return 3;
    }

    // Leaked on purpose, like ``shared``: the threads below are never
    // joined (see the end of this function).
    auto* shared = new Shared(handle);
    auto* services = new core::AgentServices{core::AgentServices::Role::SessionHelper};
    auto* ctx = new rpc::RpcContext{*services};

    shared->send({{"event", helper_topic::kHello},
                  {"pid", static_cast<std::uint64_t>(::GetCurrentProcessId())},
                  {"session", session}});

    // Sample every readiness signal for the whole boot, independently of
    // the gate. The first-logon "Preparing Windows" phase happens once
    // per install and cannot be reproduced by rebooting, so there is no
    // second chance to observe it — the trace has to be captured on the
    // run that matters. Keeps going after the gate opens, because the
    // diagnostic question is "what was still false when we let go".
    auto* probe = new win::DesktopProbe;
    probe->start(std::chrono::minutes(20), std::chrono::milliseconds(500));
    std::thread{[shared] { run_gate(*shared); }}.detach();

    std::thread pump{[shared, services] { run_pump(*shared, services->uia()); }};
    std::vector<std::thread> workers;
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([shared, ctx] { run_worker(*shared, *ctx); });
    }

    PipeCodec codec;
    std::vector<std::uint8_t> buffer(64 * 1024);
    std::vector<nlohmann::json> messages;
    int exit_code = 0;
    for (bool running = true; running;) {
        std::size_t got = 0;
        const auto r = shared->pipe.read(buffer.data(), buffer.size(), &got, shared->stop.get());
        if (r != win::OverlappedPipe::Read::Data) {
            LSW_AGENT_INFO("helper: the service {}; exiting", r == win::OverlappedPipe::Read::Closed
                                                                  ? "went away"
                                                                  : "stopped answering");
            break;
        }
        codec.feed(buffer.data(), got);
        messages.clear();
        if (!codec.drain(messages)) {
            LSW_AGENT_ERROR("helper: corrupt stream from the service; exiting");
            exit_code = 4;
            break;
        }
        for (auto& m : messages) {
            if (PipeCodec::is_request(m)) {
                {
                    const std::lock_guard lock(shared->mu);
                    shared->requests.push_back(std::move(m));
                }
                shared->cv.notify_one();
            } else if (PipeCodec::is_event(m) &&
                       m["event"].get<std::string>() == helper_topic::kShutdown) {
                LSW_AGENT_INFO("helper: the service asked this helper to exit");
                running = false;
            }
        }
    }

    ::SetEvent(shared->stop.get());
    // Not joined, and nothing torn down: a worker can be deep inside a
    // UI Automation call into a hung application, and the gate inside its
    // 30-minute wait. The process is ended outright instead (see main),
    // which is also what releases UI Automation's registrations.
    pump.detach();
    for (auto& w : workers) {
        w.detach();
    }
    return exit_code;
}

}  // namespace lsw::agent::session
