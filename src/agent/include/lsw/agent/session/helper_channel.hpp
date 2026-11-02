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
class HelperSink {
public:
    virtual ~HelperSink() = default;

    virtual void helper_event(rpc::Event event) = 0;

    virtual void helper_changed() = 0;
};

class HelperChannel {
public:
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

    [[nodiscard]] bool connected() const noexcept { return state_.load() == State::Connected; }

    [[nodiscard]] bool finished() const noexcept { return state_.load() == State::Finished; }

    [[nodiscard]] int log_level_sent() const noexcept { return log_level_sent_.load(); }
    void set_log_level_sent(int level) noexcept { log_level_sent_.store(level); }

    [[nodiscard]] bool desktop_ready() const noexcept { return desktop_ready_.load(); }

    [[nodiscard]] bool wait_connected(std::chrono::milliseconds wait);

    [[nodiscard]] nlohmann::json call(const std::string& cmd, const nlohmann::json& params,
                                      std::chrono::milliseconds timeout);

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
    std::atomic<int> log_level_sent_{-1};
    std::atomic<bool> stopping_{false};
    std::mutex state_mu_;
    std::condition_variable state_cv_;

    PendingCalls pending_;
    std::thread thread_;
};

}
