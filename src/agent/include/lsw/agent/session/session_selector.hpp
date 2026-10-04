#pragma once

#include "lsw/agent/session/session_info.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::session {

// The rules that decide which session a request lands in, which sessions
// get a helper, and which helper's "desktop ready" opens the install
// gate. Plain data in, plain answer out, so they are tested without a
// Windows machine — a wrong choice here launches an application on a
// desktop nobody is looking at.
class SessionSelector {
public:
    // ``sessions`` as WtsSessions lists them; ``wizard_user`` the account
    // the installer created (empty when it cannot be read).
    SessionSelector(std::vector<SessionInfo> sessions, std::string wizard_user);

    // OOBE signs in with throwaway accounts (``defaultuser0``,
    // ``defaultuser100000``) before the real one exists. Nothing of ours
    // belongs in those sessions, and their desktop is not the user's.
    [[nodiscard]] static bool is_setup_account(const std::string& user);

    // A session the agent keeps a helper in: someone is logged on, and it
    // is not OOBE's.
    [[nodiscard]] static bool wants_helper(const SessionInfo& session);

    // The session a request without an explicit ``session`` goes to:
    //   1. the console session, when a real user is logged on to it;
    //   2. else the wizard user's active session, then any of theirs;
    //   3. else any active session with a real user.
    // Nullopt when nobody is logged on.
    [[nodiscard]] std::optional<std::uint32_t> pick_default() const;

    // The listed session ``id`` when a real user is logged on to it.
    [[nodiscard]] std::optional<SessionInfo> find(std::uint32_t id) const;

    // Whether a helper in ``id`` reporting a usable desktop means the
    // install is finished: a real user's session that is either on the
    // console or the wizard user's own. Someone else's remote session
    // being usable says nothing about the screen the wizard is watching.
    [[nodiscard]] bool counts_for_gate(std::uint32_t id) const;

    [[nodiscard]] const std::vector<SessionInfo>& sessions() const noexcept { return sessions_; }

private:
    [[nodiscard]] bool is_wizard(const SessionInfo& session) const;

    std::vector<SessionInfo> sessions_;
    std::string wizard_user_;
};

}  // namespace lsw::agent::session
