#pragma once

namespace lsw::agent::win {

// Enables the privileges an admin token carries but leaves switched off.
//
// Windows gates ACL and driver operations behind privileges that are
// present-but-disabled by default. Well-behaved installers enable what
// they need themselves; some legacy ones do not and fail with Win32 1307
// (ERROR_INVALID_OWNER) on a SetNamedSecurityInfo. Enabling them once in
// the agent's own token covers every child it starts as itself, because
// children inherit the token. A child started as a session's user gets
// a token of its own, so RunAs enables the same set on that token.
class TokenPrivileges {
public:
    TokenPrivileges() = delete;

    // Best-effort. A privilege the current token does not hold is
    // skipped rather than treated as fatal: running the agent as a
    // non-admin during development is fine, only the install paths
    // suffer. Returns how many were enabled.
    //
    // ``token`` is a token handle opened with TOKEN_ADJUST_PRIVILEGES |
    // TOKEN_QUERY; null means this process's own.
    static int enable_install_privileges(void* token = nullptr);
};

}  // namespace lsw::agent::win
