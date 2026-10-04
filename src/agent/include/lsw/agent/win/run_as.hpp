#pragma once

#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/process.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::agent::win {

// Who a process the agent starts runs as, and in which session.
//
// The agent is a LocalSystem service in session 0. A process it starts
// with plain CreateProcessW is SYSTEM, in session 0, on a desktop nobody
// sees — right for installers and for its own tools, wrong for anything
// the user is meant to see. ``user`` is the other answer: the token of
// the user logged on to a session (WTSQueryUserToken, which only
// LocalSystem may call), that user's environment, and the session's
// interactive desktop.
//
// Elevation: installers and applications run as administrator by
// default. ``elevated`` asks for the user's linked (full) token where
// UAC split it, with the install privileges enabled.
//
// Cheap to copy: the token is shared, closed when the last copy goes. A
// job that runs later holds its own copy.
class RunAs {
public:
    // This process's identity: SYSTEM in session 0 when the agent is the
    // service, whoever started it when it runs in the foreground.
    [[nodiscard]] static RunAs self();

    // The user logged on to ``session``. Throws std::runtime_error with a
    // sentence naming what failed — nobody logged on, or the agent not
    // being LocalSystem.
    [[nodiscard]] static RunAs user(std::uint32_t session, bool elevated);

    [[nodiscard]] bool is_self() const noexcept { return token_ == nullptr; }

    [[nodiscard]] std::uint32_t session() const noexcept { return session_; }

    [[nodiscard]] bool elevated() const noexcept { return elevated_; }

    // The account, without its domain: "SYSTEM", "alice".
    [[nodiscard]] const std::string& account() const noexcept { return account_; }

    // The account's SID in string form ("S-1-5-21-...").
    [[nodiscard]] const std::wstring& sid() const noexcept { return sid_; }

    // ``%VARIABLE%`` references expanded against this identity's
    // environment — the user's profile paths, not SYSTEM's.
    [[nodiscard]] std::wstring expand(const std::wstring& s) const;

    // The environment a child started as this identity gets, with
    // ``overrides`` on top. Empty for ``self`` with no overrides (the
    // child inherits ours); never empty for a user.
    [[nodiscard]] std::vector<wchar_t>
    environment(const std::vector<Environment::Override>& overrides) const;

    // Point ``options`` at this identity: token, interactive desktop, and
    // the user's environment unless the caller already built one (from
    // ``environment``). A no-op for ``self``.
    void apply(SpawnOptions& options) const;

    // "SYSTEM in session 0", "alice in session 1 (elevated)" — for logs
    // and error messages.
    [[nodiscard]] std::string describe() const;

private:
    RunAs() = default;

    // HANDLE of a primary token; null for self.
    std::shared_ptr<void> token_;
    std::uint32_t session_ = 0;
    bool elevated_ = false;
    std::string account_;
    std::wstring sid_;
};

}  // namespace lsw::agent::win
