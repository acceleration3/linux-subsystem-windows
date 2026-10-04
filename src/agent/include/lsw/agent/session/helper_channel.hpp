#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/session/pending_calls.hpp"
#include "lsw/agent/session/session_info.hpp"
#include "lsw/agent/win/overlapped_pipe.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::session {

// What a helper reports that is not the answer to a request.
class HelperSink {
public:
    virtual ~HelperSink() = default;

    // A manager-bound event (UI Automation), to forward unchanged.
    virtual void helper_event(rpc::Event event) = 0;

    // The helper connected, reported its desktop, or went away.
    virtual void helper_changed() = 0;
};

// The service's end of one session helper: the process, started as the
// session's user, and the named pipe it dials back into.
//
// The pipe is created here, before the helper exists, with a DACL that
// admits SYSTEM and that session's user only, and as the first and only
// instance of its name — so nothing can be waiting on the name first and
// nothing else can open it after. The client that connects must be the
// process launched for it (GetNamedPipeClientProcessId), or it is turned
// away.
//
// A thread per channel connects and then reads, delivering replies to
// whoever is blocked in ``call`` and everything else to the sink. When
// the helper exits, or stops reading, every caller waiting on it is
// failed at once rather than left to time out.
class HelperChannel {
public:
    // Create the pipe and start the helper in ``info``'s session as
    // ``as``. Nullptr, with ``error`` set, when either fails.
    [[nodiscard]] static std::shared_ptr<HelperChannel>
    launch(const SessionInfo& info, const win::RunAs& as, HelperSink& sink, std::string* error);

    ~HelperChannel();

    HelperChannel(const HelperChannel&) = delete;
    HelperChannel& operator=(const HelperChannel&) = delete;

    [[nodiscard]] std::uint32_t session() const noexcept { return session_; }

    [[nodiscard]] const std::string& user() const noexcept { return user_; }

    [[nodiscard]] unsigned long pid() const noexcept { return pid_; }

    [[nodiscard]] std::chrono::steady_clock::time_point started() const noexcept {
        return started_;
    }

    // Connected and serving.
    [[nodiscard]] bool connected() const noexcept { return state_.load() == State::Connected; }

    // Gone for good: the helper exited, never connected, or was stopped.
    [[nodiscard]] bool finished() const noexcept { return state_.load() == State::Finished; }

    // The helper's desktop gate has opened.
    [[nodiscard]] bool desktop_ready() const noexcept { return desktop_ready_.load(); }

    // Block up to ``wait`` for the helper to connect. True once it has.
    [[nodiscard]] bool wait_connected(std::chrono::milliseconds wait);

    // Forward one request and return its ``result``. Throws
    // std::runtime_error with the helper's own error, or with why there
    // was no answer (not connected, gone, timed out).
    [[nodiscard]] nlohmann::json call(const std::string& cmd, const nlohmann::json& params,
                                      std::chrono::milliseconds timeout);

    // Tell the helper to exit, close the pipe, and make sure the process
    // is gone. Idempotent; not to be called from the sink callbacks.
    void stop();

private:
    enum class State { Starting, Connected, Finished };

    HelperChannel(const SessionInfo& info, HelperSink& sink);

    void run();
    void on_message(nlohmann::json message);
    [[nodiscard]] bool send(const nlohmann::json& message, std::chrono::milliseconds timeout);
    void set_state(State state);
    [[nodiscard]] std::string label() const;

    std::uint32_t session_;
    std::string user_;
    HelperSink& sink_;
    std::chrono::steady_clock::time_point started_;

    std::unique_ptr<win::OverlappedPipe> pipe_;
    win::UniqueHandle process_;
    unsigned long pid_ = 0;
    win::UniqueHandle stop_event_;

    std::atomic<State> state_{State::Starting};
    std::atomic<bool> desktop_ready_{false};
    std::atomic<bool> stopping_{false};
    std::mutex state_mu_;
    std::condition_variable state_cv_;

    PendingCalls pending_;
    std::thread thread_;
};

}  // namespace lsw::agent::session
