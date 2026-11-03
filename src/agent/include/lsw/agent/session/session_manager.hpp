#pragma once

#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/session/helper_channel.hpp"
#include "lsw/agent/session/input_relay_supervisor.hpp"
#include "lsw/agent/session/session_info.hpp"
#include "lsw/agent/session/session_selector.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lsw::agent::win {
class RunAs;
}

namespace lsw::agent::session {
class SessionManager final : public rpc::EventSource, private HelperSink {
public:
    explicit SessionManager(bool as_service);
    ~SessionManager() override;

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    void start();
    void stop();

    void notify_session_change(std::uint32_t session, std::uint32_t event);

    [[nodiscard]] bool wait_for_desktop(std::chrono::steady_clock::time_point deadline,
                                        const std::atomic<bool>& stop);

    [[nodiscard]] nlohmann::json route(const std::string& cmd, const nlohmann::json& params);

    enum class Default {
        UserSession,
        System,
    };
    [[nodiscard]] win::RunAs run_as(const nlohmann::json& params, const std::string& cmd,
                                    Default fallback);

    [[nodiscard]] win::RunAs run_as_beside(const nlohmann::json& params, const std::string& cmd,
                                           std::optional<std::uint32_t> pid);

    void sync_log_level();

    [[nodiscard]] nlohmann::json describe();

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    struct Slot {
        std::shared_ptr<HelperChannel> channel;
        std::chrono::steady_clock::time_point retry_at{};
        std::chrono::seconds backoff{1};
    };

    void supervise();
    void reconcile();
    [[nodiscard]] SessionSelector selector(bool refresh);
    [[nodiscard]] std::shared_ptr<HelperChannel> channel_for(std::uint32_t session,
                                                             std::chrono::milliseconds wait);
    [[nodiscard]] win::RunAs identity_for(std::uint32_t session, bool elevated);

    void helper_event(rpc::Event event) override;
    void helper_changed() override;

    const bool as_service_;
    const std::uint32_t own_session_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::uint32_t, Slot> slots_;
    std::vector<SessionInfo> snapshot_;
    std::chrono::steady_clock::time_point snapshot_at_{};
    std::string wizard_;
    bool wake_ = false;
    bool stopping_ = false;

    std::mutex events_mu_;
    std::deque<rpc::Event> events_;

    InputRelaySupervisor relays_;

    std::thread supervisor_;
};

}
