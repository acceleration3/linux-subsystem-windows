#pragma once

#include "lsw/agent/session/session_info.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace lsw::agent::win {

// The machine's Windows sessions, from Terminal Services (which also
// answers on a machine that has never seen a remote desktop: the console
// is a session like any other).
class WtsSessions {
public:
    WtsSessions() = delete;

    // Every session with a logged-on user, in id order. Session 0
    // (services), the remote-desktop listener and sessions being set up
    // or torn down are left out.
    [[nodiscard]] static std::vector<session::SessionInfo> list();

    // The session attached to the physical console, or nullopt while the
    // console is between sessions.
    [[nodiscard]] static std::optional<std::uint32_t> active_console();

    // The session ``pid`` runs in, or nullopt for a process that is gone.
    [[nodiscard]] static std::optional<std::uint32_t> of_process(std::uint32_t pid);

    // The session this process runs in.
    [[nodiscard]] static std::uint32_t current();
};

}  // namespace lsw::agent::win
