#pragma once

#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/session/helper_channel.hpp"
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

// The service's view of the machine's user sessions, and the session
// helper it keeps in each.
//
// A supervisor thread reconciles helpers against the sessions that
// exist: one helper per session with a real user logged on, launched as
// that user, relaunched with backoff if it dies, stopped when its
// session ends. It runs every few seconds and immediately on a
// SERVICE_CONTROL_SESSIONCHANGE, so a logon gets its helper at once and
// a missed notification costs a few seconds, not a stuck session.
//
// Three things are asked of it:
//   * routing — a session-bound command goes to the helper in the
//     session it names (or the default session) and comes back, or fails
//     with a reason; it never hangs;
//   * identities — who a process the service starts runs as (RunAs);
//   * the install gate — whether a helper in the session that matters
//     has seen a usable desktop.
//
// Also the EventSource for everything helpers push up (UI Automation),
// in place of the UIA service the agent used to own itself.
class SessionManager final : public rpc::EventSource, private HelperSink {
public:
    // ``as_service``: the agent is the LocalSystem service. Otherwise it
    // runs in the foreground for debugging, as whoever started it, and
    // acts for its own session with its own token.
    explicit SessionManager(bool as_service);
    ~SessionManager() override;

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    void start();
    // Stop the supervisor and every helper. Idempotent.
    void stop();

    // A SERVICE_CONTROL_SESSIONCHANGE arrived: reconcile now.
    void notify_session_change(std::uint32_t session, std::uint32_t event);

    // Block until a helper in a session that counts for the install gate
    // (SessionSelector::counts_for_gate) reports a usable desktop, until
    // ``deadline``, or until ``stop`` is set. True only for the first.
    [[nodiscard]] bool wait_for_desktop(std::chrono::steady_clock::time_point deadline,
                                        const std::atomic<bool>& stop);

    // Forward a session-bound command and return its result. Throws
    // std::runtime_error ("no user session N", "the session helper in
    // session N is not running", a timeout) or std::invalid_argument
    // (a malformed ``session``).
    [[nodiscard]] nlohmann::json route(const std::string& cmd, const nlohmann::json& params);

    // Where a process the service starts for ``cmd`` runs.
    enum class Default {
        UserSession,  // the default session's user, as for ``launch``
        System,       // SYSTEM in session 0, as for install jobs
    };
    // ``params.session`` picks the session (0 = SYSTEM in session 0);
    // without it, ``fallback``. ``params.elevated`` (default true) picks
    // the user's full token. Throws std::runtime_error when there is no
    // such user session.
    [[nodiscard]] win::RunAs run_as(const nlohmann::json& params, const std::string& cmd,
                                    Default fallback);

    // Like ``run_as`` with Default::UserSession, but defaulting to the
    // session ``pid`` runs in when it is a user session.
    [[nodiscard]] win::RunAs run_as_beside(const nlohmann::json& params, const std::string& cmd,
                                           std::optional<std::uint32_t> pid);

    // ``list_sessions``: {"sessions": [{id, user, state, console, helper}],
    //                     "default": id|null}
    [[nodiscard]] nlohmann::json describe();

    // EventSource: whatever the helpers have pushed since the last call.
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    struct Slot {
        std::shared_ptr<HelperChannel> channel;
        // When a new helper may be launched in this session after the last
        // one died, and how long the next wait will be.
        std::chrono::steady_clock::time_point retry_at{};
        std::chrono::seconds backoff{1};
    };

    void supervise();
    void reconcile();
    [[nodiscard]] SessionSelector selector(bool refresh);
    [[nodiscard]] std::shared_ptr<HelperChannel> channel_for(std::uint32_t session,
                                                             std::chrono::milliseconds wait);
    [[nodiscard]] win::RunAs identity_for(std::uint32_t session, bool elevated);

    // HelperSink
    void helper_event(rpc::Event event) override;
    void helper_changed() override;

    const bool as_service_;
    const std::uint32_t own_session_;

    std::mutex mu_;  // slots_, snapshot_, snapshot_at_, wizard_
    std::condition_variable cv_;
    std::map<std::uint32_t, Slot> slots_;
    std::vector<SessionInfo> snapshot_;
    std::chrono::steady_clock::time_point snapshot_at_{};
    std::string wizard_;
    bool wake_ = false;
    bool stopping_ = false;

    std::mutex events_mu_;
    std::deque<rpc::Event> events_;

    std::thread supervisor_;
};

}  // namespace lsw::agent::session
