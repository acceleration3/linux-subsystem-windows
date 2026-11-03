#include "lsw/agent/session/session_host.hpp"

#include "lsw/agent/core/services.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/context.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/session/helper_protocol.hpp"
#include "lsw/agent/session/pipe_codec.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/agent/win/clipboard_watcher.hpp"
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
constexpr int kWorkers = 4;

constexpr auto kPumpTick = std::chrono::milliseconds(16);

constexpr auto kDialFor = std::chrono::seconds(30);

DWORD parent_pid() {
    const DWORD self = ::GetCurrentProcessId();
    for (const auto& e : win::ProcessInfo::snapshot()) {
        if (e.pid == self) {
            return e.parent;
        }
    }
    return 0;
}

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

struct Shared {
    explicit Shared(HANDLE handle)
        : pipe(handle), stop(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    win::OverlappedPipe pipe;
    win::UniqueHandle stop;

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

nlohmann::json serve(const nlohmann::json& request, rpc::RpcContext& ctx) {
    const auto id = request.value("id", std::uint64_t{0});
    const auto cmd = request.value("cmd", std::string{});
    const auto params = (request.contains("params") && request["params"].is_object())
                            ? request["params"]
                            : nlohmann::json::object();
    nlohmann::json reply = {{"id", id}};
    if (cmd == helper_cmd::kSetLogLevel) {
        log::set_level(log::level_from_name(params.value("level", std::string{})));
        reply["result"] = {{"ok", true}};
        return reply;
    }
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

void run_gate(Shared& shared) {
    const auto started = std::chrono::steady_clock::now();
    const bool ready = win::DesktopSession::wait_for_desktop(
        std::chrono::minutes(30), std::chrono::milliseconds(500), std::chrono::seconds(3),
        std::chrono::seconds(90));
    if (!ready) {
        LSW_AGENT_WARN("helper: desktop never became usable after 30 min");
    }
    const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started);
    shared.send({{"event", helper_topic::kDesktop},
                 {"ready", ready},
                 {"waited_s", static_cast<std::int64_t>(waited.count())}});
}

void run_pump(Shared& shared, uia::Uia& uia, win::ClipboardWatcher* clipboard,
              std::uint32_t session) {
    std::uint64_t log_cursor = 0;
    while (!shared.stopping()) {
        for (const auto& record : log::collect_since(log_cursor, log_cursor)) {
            shared.send({{"event", helper_topic::kLog},
                         {"level", log::level_name(record.level)},
                         {"ts_ms", record.ts_ms},
                         {"msg", record.msg}});
        }
        for (auto& ev : uia.collect_pending()) {
            nlohmann::json message = std::move(ev.payload);
            message["event"] = ev.topic;
            shared.send(message);
        }
        if (clipboard != nullptr) {
            for (auto& ev : clipboard->collect_pending()) {
                nlohmann::json message = std::move(ev.payload);
                message["event"] = ev.topic;
                message["session"] = session;
                shared.send(message);
            }
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

}

int SessionHost::run(const std::wstring& pipe_name) {
    const std::uint32_t session = win::WtsSessions::current();
    LSW_AGENT_INFO("helper: start in session {} (pid {})", session, ::GetCurrentProcessId());

    const HANDLE handle = dial(pipe_name);
    if (handle == INVALID_HANDLE_VALUE) {
        return 2;
    }
    ULONG server_pid = 0;
    const DWORD parent = parent_pid();
    if (::GetNamedPipeServerProcessId(handle, &server_pid) == FALSE || server_pid != parent) {
        LSW_AGENT_ERROR("helper: {} is served by pid {}, not by the agent that started this "
                        "helper (pid {}); refusing it",
                        pipe_name, server_pid, parent);
        ::CloseHandle(handle);
        return 3;
    }

    auto* shared = new Shared(handle);
    auto* services = new core::AgentServices{core::AgentServices::Role::SessionHelper};
    auto* ctx = new rpc::RpcContext{*services};

    shared->send({{"event", helper_topic::kHello},
                  {"pid", static_cast<std::uint64_t>(::GetCurrentProcessId())},
                  {"session", session}});

    std::thread{[shared] { run_gate(*shared); }}.detach();

    std::thread pump{[shared, services, session] {
        run_pump(*shared, services->uia(), services->clipboard(), session);
    }};
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
    pump.detach();
    for (auto& w : workers) {
        w.detach();
    }
    return exit_code;
}

}
