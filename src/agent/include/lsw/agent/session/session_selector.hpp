#pragma once

#include "lsw/agent/session/session_info.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::session {
class SessionSelector {
public:
    SessionSelector(std::vector<SessionInfo> sessions, std::string wizard_user);

    [[nodiscard]] static bool is_setup_account(const std::string& user);

    [[nodiscard]] static bool wants_helper(const SessionInfo& session);

    [[nodiscard]] std::optional<std::uint32_t> pick_default() const;

    [[nodiscard]] std::optional<SessionInfo> find(std::uint32_t id) const;

    [[nodiscard]] bool counts_for_gate(std::uint32_t id) const;

    [[nodiscard]] const std::vector<SessionInfo>& sessions() const noexcept { return sessions_; }

private:
    [[nodiscard]] bool is_wizard(const SessionInfo& session) const;

    std::vector<SessionInfo> sessions_;
    std::string wizard_user_;
};

}
