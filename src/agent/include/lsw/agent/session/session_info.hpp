#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::session {

// Where a Windows session stands, reduced to what the manager can act
// on. The Terminal Services states that are not a user's session at all
// — the listener, a session being torn down or still initialising — are
// never reported, so they have no value here.
enum class SessionState {
    Active,
    Connected,
    Disconnected,
};

[[nodiscard]] const char* state_name(SessionState state);

// One interactive Windows session, as WtsSessions reports it. Session 0
// (services) is never one of these: nothing has a desktop there.
struct SessionInfo {
    std::uint32_t id = 0;
    // The account logged on to it, without the domain. Empty when the
    // session has no user yet (a console at the logon screen).
    std::string user;
    std::string domain;
    SessionState state = SessionState::Disconnected;
    // Attached to the physical console — the one the VM's display shows.
    bool console = false;
};

}  // namespace lsw::agent::session
