#include "lsw/agent/session/session_selector.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace lsw::agent::session {

namespace {

// Windows account names compare case-insensitively. ASCII folding is
// enough for the comparisons made here: the wizard's account name is
// typed into the manager, and OOBE's accounts are ASCII.
[[nodiscard]] std::string fold(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

const char* state_name(SessionState state) {
    switch (state) {
    case SessionState::Active:
        return "active";
    case SessionState::Connected:
        return "connected";
    case SessionState::Disconnected:
        return "disconnected";
    }
    return "disconnected";
}

SessionSelector::SessionSelector(std::vector<SessionInfo> sessions, std::string wizard_user)
    : sessions_(std::move(sessions)), wizard_user_(fold(std::move(wizard_user))) {}

bool SessionSelector::is_setup_account(const std::string& user) {
    return fold(user).starts_with("defaultuser");
}

bool SessionSelector::wants_helper(const SessionInfo& session) {
    return session.id != 0 && !session.user.empty() && !is_setup_account(session.user);
}

bool SessionSelector::is_wizard(const SessionInfo& session) const {
    return !wizard_user_.empty() && fold(session.user) == wizard_user_;
}

std::optional<std::uint32_t> SessionSelector::pick_default() const {
    for (const auto& s : sessions_) {
        if (s.console && wants_helper(s)) {
            return s.id;
        }
    }
    for (const auto& s : sessions_) {
        if (wants_helper(s) && is_wizard(s) && s.state == SessionState::Active) {
            return s.id;
        }
    }
    for (const auto& s : sessions_) {
        if (wants_helper(s) && is_wizard(s)) {
            return s.id;
        }
    }
    for (const auto& s : sessions_) {
        if (wants_helper(s) && s.state == SessionState::Active) {
            return s.id;
        }
    }
    return std::nullopt;
}

std::optional<SessionInfo> SessionSelector::find(std::uint32_t id) const {
    for (const auto& s : sessions_) {
        if (s.id == id && wants_helper(s)) {
            return s;
        }
    }
    return std::nullopt;
}

bool SessionSelector::counts_for_gate(std::uint32_t id) const {
    const auto s = find(id);
    return s && (s->console || is_wizard(*s));
}

}  // namespace lsw::agent::session
