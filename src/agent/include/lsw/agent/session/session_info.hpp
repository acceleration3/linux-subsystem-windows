#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::session {
enum class SessionState {
    Active,
    Connected,
    Disconnected,
};

[[nodiscard]] const char* state_name(SessionState state);

struct SessionInfo {
    std::uint32_t id = 0;
    std::string user;
    std::string domain;
    SessionState state = SessionState::Disconnected;
    bool console = false;
};

}
