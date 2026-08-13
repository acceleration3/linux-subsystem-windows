#pragma once

namespace lsw::agent::win {

// Enables the privileges an admin token carries but leaves switched off.
//
// Windows gates ACL and driver operations behind privileges that are
// present-but-disabled by default. Well-behaved installers enable what
// they need themselves; some legacy ones do not and fail with Win32 1307
// (ERROR_INVALID_OWNER) on a SetNamedSecurityInfo. Enabling them once in
// the agent covers every child it spawns — download_install, run_script,
// console_open — because children inherit the token.
class TokenPrivileges {
public:
    TokenPrivileges() = delete;

    // Best-effort. A privilege the current token does not hold is
    // skipped rather than treated as fatal: running the agent as a
    // non-admin during development is fine, only the install paths
    // suffer. Returns how many were enabled.
    static int enable_install_privileges();
};

}  // namespace lsw::agent::win
