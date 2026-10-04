#include "lsw/agent/session/helper_channel.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/session/helper_protocol.hpp"
#include "lsw/agent/session/pipe_codec.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/agent/win/security_attributes.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <stdexcept>
#include <utility>
#include <vector>

namespace lsw::agent::session {

namespace {

// Big enough for any request; a helper's large replies (a UIA tree)
// simply take more than one buffer's worth of writes.
constexpr DWORD kPipeBuffer = 64 * 1024;

// How long a stopping helper gets to leave on its own before it is
// terminated.
constexpr DWORD kExitGraceMs = 2000;

// A pipe name nobody can predict or have created first. The session id
// keeps it readable in a handle list; the rest makes each launch's name
// unique, so a helper from a previous launch can never dial the new one.
std::wstring pipe_name(std::uint32_t session) {
    static std::atomic<std::uint64_t> counter{0};
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    const std::uint64_t nonce = static_cast<std::uint64_t>(now.QuadPart) ^
                                (counter.fetch_add(1) << 48) ^ ::GetCurrentProcessId();
    return L"\\\\.\\pipe\\lsw-agent-session-" + std::to_wstring(session) + L"-" +
           std::to_wstring(nonce);
}

}  // namespace

HelperChannel::HelperChannel(const SessionInfo& info, HelperSink& sink)
    : session_(info.id), user_(info.user), sink_(sink), started_(std::chrono::steady_clock::now()),
      stop_event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

std::shared_ptr<HelperChannel> HelperChannel::launch(const SessionInfo& info, const win::RunAs& as,
                                                     HelperSink& sink, std::string* error) {
    const auto fail = [&](std::string why) -> std::shared_ptr<HelperChannel> {
        if (error != nullptr) {
            *error = std::move(why);
        }
        return nullptr;
    };
    if (as.sid().empty()) {
        return fail("no SID for " + as.describe());
    }

    // SYSTEM (the service) and the session's user (the helper) — nobody
    // else. GRGW is what CreateFile(GENERIC_READ | GENERIC_WRITE) asks.
    win::SecurityAttributes security{L"D:P(A;;GA;;;SY)(A;;GRGW;;;" + as.sid() + L")"};
    if (!security.ok()) {
        return fail("cannot build the pipe's security descriptor");
    }
    const std::wstring name = pipe_name(info.id);
    HANDLE pipe = ::CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        kPipeBuffer, kPipeBuffer, 0, security.get());
    if (pipe == INVALID_HANDLE_VALUE) {
        return fail("CreateNamedPipe failed: " + win::ErrorText::last_error());
    }

    std::shared_ptr<HelperChannel> channel{new HelperChannel(info, sink)};
    channel->pipe_ = std::make_unique<win::OverlappedPipe>(pipe);
    if (!channel->stop_event_) {
        return fail("CreateEvent failed: " + win::ErrorText::last_error());
    }

    const std::wstring& self = win::ProcessInfo::self_path();
    win::SpawnOptions options;
    options.application = self;
    options.command_line =
        win::Process::quote(self) + L" --session --pipe " + win::Process::quote(name);
    // The helper is a GUI-subsystem program that never makes a window;
    // CREATE_NO_WINDOW is belt and braces should that ever change.
    options.creation_flags = CREATE_NO_WINDOW;
    options.working_dir = core::Paths::root();
    try {
        as.apply(options);
    } catch (const std::exception& e) {
        return fail(std::string{"cannot start the helper as "} + as.describe() + ": " + e.what());
    }
    DWORD spawn_error = 0;
    auto spawned = win::Process::spawn(options, &spawn_error);
    if (!spawned) {
        return fail("CreateProcess as " + as.describe() +
                    " failed: " + win::ErrorText::win32(spawn_error));
    }
    channel->process_ = std::move(spawned->process);
    channel->pid_ = spawned->pid;

    LSW_AGENT_INFO("sessions: started helper pid={} for session {} as {}", channel->pid_, info.id,
                   as.describe());
    channel->thread_ = std::thread([raw = channel.get()] { raw->run(); });
    return channel;
}

HelperChannel::~HelperChannel() {
    stop();
}

std::string HelperChannel::label() const {
    return "the session helper in session " + std::to_string(session_);
}

void HelperChannel::set_state(State state) {
    {
        const std::lock_guard lock(state_mu_);
        state_.store(state);
    }
    state_cv_.notify_all();
    sink_.helper_changed();
}

bool HelperChannel::wait_connected(std::chrono::milliseconds wait) {
    std::unique_lock lock(state_mu_);
    state_cv_.wait_for(lock, wait, [&] { return state_.load() != State::Starting; });
    return state_.load() == State::Connected;
}

void HelperChannel::run() {
    // Connect, racing the helper's own exit and a stop: a helper that
    // dies on startup must not hold its session's slot for the full
    // timeout.
    const auto connected = pipe_->connect(kHelperConnectTimeout, stop_event_.get(), process_.get());

    if (connected != win::OverlappedPipe::Wait::Done) {
        DWORD code = STILL_ACTIVE;
        ::GetExitCodeProcess(process_.get(), &code);
        LSW_AGENT_WARN("sessions: {} (pid {}) never connected ({}){}", label(), pid_,
                       connected == win::OverlappedPipe::Wait::TimedOut ? "timed out"
                       : connected == win::OverlappedPipe::Wait::Interrupted
                           ? "it exited, or the agent is stopping"
                           : "pipe failed",
                       code == STILL_ACTIVE ? std::string{} : " exit code " + std::to_string(code));
        pending_.close(label() + " is not running");
        set_state(State::Finished);
        return;
    }

    ULONG client_pid = 0;
    if (::GetNamedPipeClientProcessId(pipe_->get(), &client_pid) == FALSE || client_pid != pid_) {
        LSW_AGENT_ERROR("sessions: refused a client on {}'s pipe: pid {} is not the helper "
                        "(pid {})",
                        label(), client_pid, pid_);
        ::DisconnectNamedPipe(pipe_->get());
        pending_.close(label() + " is not running");
        set_state(State::Finished);
        return;
    }
    LSW_AGENT_INFO("sessions: {} connected (pid {})", label(), pid_);
    set_state(State::Connected);

    PipeCodec codec;
    std::vector<std::uint8_t> buffer(kPipeBuffer);
    std::vector<nlohmann::json> messages;
    for (;;) {
        std::size_t got = 0;
        const auto r = pipe_->read(buffer.data(), buffer.size(), &got, stop_event_.get());
        if (r != win::OverlappedPipe::Read::Data) {
            if (r == win::OverlappedPipe::Read::Closed) {
                LSW_AGENT_WARN("sessions: {} (pid {}) went away", label(), pid_);
            }
            break;
        }
        codec.feed(buffer.data(), got);
        messages.clear();
        if (!codec.drain(messages)) {
            LSW_AGENT_ERROR("sessions: {} sent a corrupt stream; dropping it", label());
            break;
        }
        for (auto& m : messages) {
            on_message(std::move(m));
        }
    }
    pending_.close(label() + " went away");
    desktop_ready_.store(false);
    set_state(State::Finished);
}

void HelperChannel::on_message(nlohmann::json message) {
    if (PipeCodec::is_reply(message)) {
        const auto id = message.value("id", std::uint64_t{0});
        (void)pending_.complete(id, std::move(message));
        return;
    }
    if (!PipeCodec::is_event(message)) {
        return;
    }
    const std::string topic = message["event"].get<std::string>();
    if (topic == helper_topic::kLog) {
        log::append(log::level_from_name(message.value("level", std::string{"INFO"})),
                    "session " + std::to_string(session_) + ": " +
                        message.value("msg", std::string{}));
        return;
    }
    if (topic == helper_topic::kHello) {
        LSW_AGENT_INFO("sessions: {} says hello (pid {})", label(),
                       message.value("pid", std::uint64_t{0}));
        return;
    }
    if (topic == helper_topic::kDesktop) {
        const bool ready = message.value("ready", false);
        LSW_AGENT_INFO("sessions: {} reports the desktop {} after {}s", label(),
                       ready ? "ready" : "NOT ready (its gate gave up)",
                       message.value("waited_s", std::int64_t{0}));
        desktop_ready_.store(ready);
        sink_.helper_changed();
        return;
    }
    // Anything else is the manager's: a UI Automation event, forwarded
    // as it came.
    message.erase("event");
    sink_.helper_event(rpc::Event{topic, std::move(message)});
}

bool HelperChannel::send(const nlohmann::json& message, std::chrono::milliseconds timeout) {
    const std::string bytes = PipeCodec::encode(message);
    if (pipe_->write_all(bytes.data(), bytes.size(), timeout)) {
        return true;
    }
    // A helper that stops reading is as good as gone; end the reader too,
    // so everyone waiting on it hears so now.
    ::SetEvent(stop_event_.get());
    return false;
}

nlohmann::json HelperChannel::call(const std::string& cmd, const nlohmann::json& params,
                                   std::chrono::milliseconds timeout) {
    if (!connected()) {
        throw std::runtime_error(cmd + ": " + label() + " is not running");
    }
    const std::uint64_t id = pending_.open();
    if (!send({{"id", id}, {"cmd", cmd}, {"params", params}},
              std::chrono::duration_cast<std::chrono::milliseconds>(kPipeWriteTimeout))) {
        pending_.cancel(id);
        throw std::runtime_error(cmd + ": " + label() + " stopped reading");
    }
    std::optional<nlohmann::json> reply;
    try {
        reply = pending_.wait(id, timeout);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(cmd + ": " + e.what());
    }
    if (!reply) {
        throw std::runtime_error(cmd + ": " + label() + " did not answer within " +
                                 std::to_string(timeout.count() / 1000) + "s");
    }
    if (const auto it = reply->find("error"); it != reply->end()) {
        throw std::runtime_error(it->is_string() ? it->get<std::string>() : it->dump());
    }
    return reply->value("result", nlohmann::json::object());
}

void HelperChannel::stop() {
    if (stopping_.exchange(true)) {
        return;
    }
    if (connected()) {
        (void)send({{"event", helper_topic::kShutdown}}, std::chrono::milliseconds(1000));
    }
    // Ends the reader (or the wait for a connection) promptly.
    ::SetEvent(stop_event_.get());
    if (thread_.joinable()) {
        thread_.join();
    }
    pending_.close(label() + " was stopped");
    // Break the helper's end too, in case it missed the shutdown message;
    // a helper whose pipe breaks exits. The handle itself stays open until
    // the last caller holding this channel has let go of it.
    if (pipe_) {
        ::DisconnectNamedPipe(pipe_->get());
    }
    if (process_ && ::WaitForSingleObject(process_.get(), kExitGraceMs) == WAIT_TIMEOUT) {
        LSW_AGENT_WARN("sessions: {} (pid {}) did not exit; terminating it", label(), pid_);
        ::TerminateProcess(process_.get(), 1);
        ::WaitForSingleObject(process_.get(), kExitGraceMs);
    }
}

}  // namespace lsw::agent::session
