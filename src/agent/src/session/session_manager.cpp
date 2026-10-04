#include "lsw/agent/session/session_manager.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/session/helper_protocol.hpp"
#include "lsw/agent/session/session_routing.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/agent/win/wts_sessions.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace lsw::agent::session {

namespace {

// How often sessions are reconciled with no notification to prompt it.
constexpr auto kReconcileEvery = std::chrono::seconds(5);

// How stale the cached session list may be for routing. list_windows is
// polled at tens of hertz; enumerating Terminal Services sessions for
// each poll would be most of its cost.
constexpr auto kSnapshotMaxAge = std::chrono::seconds(2);

// Relaunch backoff for a helper that keeps dying, and how long one must
// have lived for the backoff to start over.
constexpr auto kMaxBackoff = std::chrono::seconds(60);
constexpr auto kHealthyAfter = std::chrono::seconds(60);

// Helper-pushed events are held this many at most while no manager is
// connected to drain them; the oldest go first.
constexpr std::size_t kEventCapacity = 1000;

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return c > ' '; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// The account the installer created and logs on automatically: the
// AutoLogon user Winlogon keeps, else what UserOnce.ps1 recorded in
// user.txt. Empty when neither is there (a guest not installed by LSW).
std::string read_wizard_user() {
    if (const auto key = win::RegKey::open(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
            KEY_READ | KEY_WOW64_64KEY)) {
        if (const auto name = key->read_string(L"DefaultUserName"); name && !name->empty()) {
            return trim(util::TextEncoding::to_utf8(*name));
        }
    }
    std::ifstream file(std::filesystem::path{core::Paths::root() + L"\\user.txt"});
    std::string line;
    if (file && std::getline(file, line)) {
        return trim(line);
    }
    return {};
}

}  // namespace

SessionManager::SessionManager(bool as_service)
    : as_service_(as_service), own_session_(win::WtsSessions::current()) {}

SessionManager::~SessionManager() {
    stop();
}

void SessionManager::start() {
    {
        const std::lock_guard lock(mu_);
        wizard_ = read_wizard_user();
    }
    LSW_AGENT_INFO("sessions: supervisor starting ({}, wizard user '{}')",
                   as_service_ ? "service" : "foreground", wizard_);
    supervisor_ = std::thread([this] { supervise(); });
}

void SessionManager::stop() {
    {
        const std::lock_guard lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (supervisor_.joinable()) {
        supervisor_.join();
    }
    std::map<std::uint32_t, Slot> slots;
    {
        const std::lock_guard lock(mu_);
        slots.swap(slots_);
    }
    // Outside the lock: a channel's thread calls back into this object
    // while it winds down.
    for (auto& [id, slot] : slots) {
        if (slot.channel) {
            slot.channel->stop();
        }
    }
}

void SessionManager::notify_session_change(std::uint32_t session, std::uint32_t event) {
    LSW_AGENT_INFO("sessions: session change event={} session={}", event, session);
    {
        const std::lock_guard lock(mu_);
        wake_ = true;
    }
    cv_.notify_all();
}

void SessionManager::helper_changed() {
    // No lock: this runs on a channel's thread, which a caller of
    // HelperChannel::stop may be joining. Every waiter re-checks on a
    // short timeout, so a notification racing a wait costs that much.
    cv_.notify_all();
}

void SessionManager::helper_event(rpc::Event event) {
    const std::lock_guard lock(events_mu_);
    if (events_.size() >= kEventCapacity) {
        events_.pop_front();
    }
    events_.push_back(std::move(event));
}

std::vector<rpc::Event> SessionManager::collect_pending() {
    std::vector<rpc::Event> out;
    const std::lock_guard lock(events_mu_);
    out.reserve(events_.size());
    for (auto& e : events_) {
        out.push_back(std::move(e));
    }
    events_.clear();
    return out;
}

void SessionManager::supervise() {
    for (;;) {
        reconcile();
        std::unique_lock lock(mu_);
        cv_.wait_for(lock, kReconcileEvery, [&] { return stopping_ || wake_; });
        if (stopping_) {
            return;
        }
        wake_ = false;
    }
}

SessionSelector SessionManager::selector(bool refresh) {
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(mu_);
        if (!refresh && now - snapshot_at_ < kSnapshotMaxAge) {
            return SessionSelector{snapshot_, wizard_};
        }
    }
    auto sessions = win::WtsSessions::list();
    const std::lock_guard lock(mu_);
    if (wizard_.empty()) {
        wizard_ = read_wizard_user();  // UserOnce.ps1 may have written it since
    }
    snapshot_ = sessions;
    snapshot_at_ = now;
    return SessionSelector{std::move(sessions), wizard_};
}

win::RunAs SessionManager::identity_for(std::uint32_t session, bool elevated) {
    // In the foreground the agent cannot borrow anyone's token
    // (WTSQueryUserToken is LocalSystem's); for its own session it does
    // not need to.
    if (!as_service_ && session == own_session_) {
        return win::RunAs::self();
    }
    return win::RunAs::user(session, elevated);
}

void SessionManager::reconcile() {
    const SessionSelector sel = selector(true);
    const auto now = std::chrono::steady_clock::now();

    std::vector<std::shared_ptr<HelperChannel>> retired;
    std::vector<SessionInfo> wanted;
    {
        const std::lock_guard lock(mu_);
        if (stopping_) {
            return;
        }
        // Sessions that ended, or whose user changed, lose their helper.
        for (auto it = slots_.begin(); it != slots_.end();) {
            const auto info = sel.find(it->first);
            if (!info || (it->second.channel && it->second.channel->user() != info->user)) {
                if (it->second.channel) {
                    retired.push_back(std::move(it->second.channel));
                }
                it = slots_.erase(it);
                continue;
            }
            ++it;
        }
        for (const auto& info : sel.sessions()) {
            if (!SessionSelector::wants_helper(info)) {
                continue;
            }
            Slot& slot = slots_[info.id];
            if (slot.channel && !slot.channel->finished()) {
                continue;  // starting or serving
            }
            if (slot.channel) {
                // It died. Back off before the next attempt, unless it had
                // been running long enough to count as healthy.
                const bool healthy = now - slot.channel->started() >= kHealthyAfter;
                slot.backoff = healthy
                                   ? std::chrono::seconds(1)
                                   : std::min<std::chrono::seconds>(slot.backoff * 2, kMaxBackoff);
                slot.retry_at = now + slot.backoff;
                retired.push_back(std::move(slot.channel));
                LSW_AGENT_WARN("sessions: helper in session {} ended; relaunching in {}s", info.id,
                               slot.backoff.count());
            }
            if (now >= slot.retry_at) {
                wanted.push_back(info);
            }
        }
    }
    for (auto& channel : retired) {
        channel->stop();
    }

    for (const auto& info : wanted) {
        std::string error;
        std::shared_ptr<HelperChannel> channel;
        try {
            channel = HelperChannel::launch(info, identity_for(info.id, true), *this, &error);
        } catch (const std::exception& e) {
            error = e.what();
        }
        const std::lock_guard lock(mu_);
        if (stopping_) {
            if (channel) {
                retired.push_back(channel);  // stopped below, outside the lock
            }
            continue;
        }
        Slot& slot = slots_[info.id];
        if (channel) {
            slot.channel = std::move(channel);
        } else {
            slot.backoff = std::min<std::chrono::seconds>(slot.backoff * 2, kMaxBackoff);
            slot.retry_at = std::chrono::steady_clock::now() + slot.backoff;
            LSW_AGENT_WARN("sessions: cannot start a helper in session {} ({}): {}; retrying in "
                           "{}s",
                           info.id, info.user, error, slot.backoff.count());
        }
    }
    // Launched as the agent was stopping: not kept.
    for (auto& channel : retired) {
        if (channel) {
            channel->stop();
        }
    }
}

std::shared_ptr<HelperChannel> SessionManager::channel_for(std::uint32_t session,
                                                           std::chrono::milliseconds wait) {
    const auto deadline = std::chrono::steady_clock::now() + wait;
    for (;;) {
        std::shared_ptr<HelperChannel> channel;
        {
            const std::lock_guard lock(mu_);
            if (const auto it = slots_.find(session); it != slots_.end()) {
                channel = it->second.channel;
            }
        }
        if (channel && channel->connected()) {
            return channel;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return nullptr;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (channel && !channel->finished()) {
            (void)channel->wait_connected(left);  // being started: wait for it
            continue;
        }
        // None, or a dead one: have the supervisor look now rather than
        // at its next tick, and check again shortly.
        std::unique_lock lock(mu_);
        wake_ = true;
        cv_.notify_all();
        cv_.wait_for(lock, std::min(left, std::chrono::milliseconds(250)));
    }
}

nlohmann::json SessionManager::route(const std::string& cmd, const nlohmann::json& params) {
    const auto requested = SessionRouting::target(params, cmd);
    std::uint32_t session = 0;
    if (requested) {
        if (*requested == 0) {
            throw std::runtime_error(cmd + ": session 0 has no desktop; this command needs a "
                                           "user session");
        }
        auto sel = selector(false);
        if (!sel.find(*requested)) {
            sel = selector(true);
        }
        if (!sel.find(*requested)) {
            throw std::runtime_error(cmd + ": no user session " + std::to_string(*requested));
        }
        session = *requested;
    } else {
        const auto chosen = selector(false).pick_default();
        if (!chosen) {
            throw std::runtime_error(cmd + ": no user is logged on");
        }
        session = *chosen;
    }

    const auto channel = channel_for(session, kHelperStartWait);
    if (!channel) {
        throw std::runtime_error(cmd + ": the session helper in session " +
                                 std::to_string(session) + " is not running");
    }
    nlohmann::json reply =
        channel->call(cmd, SessionRouting::params_for_helper(params), kHelperCallTimeout);
    SessionRouting::globalize_reply(reply, session);
    return reply;
}

win::RunAs SessionManager::run_as(const nlohmann::json& params, const std::string& cmd,
                                  Default fallback) {
    const auto requested = SessionRouting::requested(params, cmd);
    const bool elevated = SessionRouting::elevated(params);
    if (requested && *requested == 0) {
        return win::RunAs::self();
    }
    if (requested) {
        return identity_for(*requested, elevated);
    }
    if (fallback == Default::System) {
        return win::RunAs::self();
    }
    const auto chosen = selector(false).pick_default();
    if (!chosen) {
        throw std::runtime_error(cmd + ": no user is logged on; pass \"session\": 0 to run "
                                       "as SYSTEM in session 0");
    }
    return identity_for(*chosen, elevated);
}

win::RunAs SessionManager::run_as_beside(const nlohmann::json& params, const std::string& cmd,
                                         std::optional<std::uint32_t> pid) {
    if (!SessionRouting::requested(params, cmd) && pid && *pid != 0) {
        if (const auto s = win::WtsSessions::of_process(*pid); s && *s != 0) {
            return identity_for(*s, SessionRouting::elevated(params));
        }
    }
    return run_as(params, cmd, Default::UserSession);
}

bool SessionManager::wait_for_desktop(std::chrono::steady_clock::time_point deadline,
                                      const std::atomic<bool>& stop) {
    constexpr auto kTraceEvery = std::chrono::seconds(30);
    auto next_trace = std::chrono::steady_clock::now() + kTraceEvery;
    for (;;) {
        if (stop.load()) {
            return false;
        }
        const SessionSelector sel = selector(false);
        std::vector<std::shared_ptr<HelperChannel>> channels;
        {
            const std::lock_guard lock(mu_);
            for (const auto& [id, slot] : slots_) {
                if (slot.channel) {
                    channels.push_back(slot.channel);
                }
            }
        }
        for (const auto& c : channels) {
            if (c->desktop_ready() && sel.counts_for_gate(c->session())) {
                LSW_AGENT_INFO("sessions: desktop ready in session {} ({})", c->session(),
                               c->user());
                return true;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        if (now >= next_trace) {
            next_trace = now + kTraceEvery;
            std::string state;
            for (const auto& s : sel.sessions()) {
                state += " " + std::to_string(s.id) + ":" + s.user + "/" + state_name(s.state) +
                         (s.console ? "/console" : "") + (sel.counts_for_gate(s.id) ? "/gate" : "");
            }
            LSW_AGENT_INFO("sessions: still waiting for a usable desktop; sessions:{}",
                           state.empty() ? std::string{" (none)"} : state);
        }
        std::unique_lock lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(500));
    }
}

nlohmann::json SessionManager::describe() {
    const SessionSelector sel = selector(true);
    nlohmann::json list = nlohmann::json::array();
    std::map<std::uint32_t, bool> serving;
    {
        const std::lock_guard lock(mu_);
        for (const auto& [id, slot] : slots_) {
            serving[id] = slot.channel && slot.channel->connected();
        }
    }
    for (const auto& s : sel.sessions()) {
        list.push_back({
            {"id", s.id},
            {"user", s.user},
            {"state", state_name(s.state)},
            {"console", s.console},
            {"helper", serving.count(s.id) != 0 && serving[s.id]},
        });
    }
    const auto chosen = sel.pick_default();
    return {
        {"sessions", std::move(list)},
        {"default", chosen ? nlohmann::json(*chosen) : nlohmann::json(nullptr)},
    };
}

}  // namespace lsw::agent::session
