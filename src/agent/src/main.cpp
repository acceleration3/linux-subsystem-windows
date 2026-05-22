// lsw-guest-agent entry point.
//
// Autostarted from ``HKLM\...\Run\lsw-agent`` (the value is written
// in the autounattend's Specialize.ps1; see autounattend.xml). The
// shell processes Run values only at a real INTERACTIVE logon — the
// post-OOBE AutoLogon of the wizard user — so the agent runs AS THAT
// USER, in their session (window enumeration/capture and app launch
// are native — plain CreateProcess, no SYSTEM, no session bridging),
// and only after OOBE, so there is no install-sentinel gate to wait
// on. The wizard account is a local admin, so the agent is elevated
// enough for installer setup steps.

#include "lsw/agent/core/agent_service.hpp"
#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <string>

namespace {

// Enable the privileges that admin/SYSTEM tokens carry but leave
// disabled by default — the agent runs as SYSTEM, but Windows still
// gates ACL and driver operations behind explicitly-enabled
// privileges. Most well-behaved installers call AdjustTokenPrivileges
// themselves; some legacy ones don't and fail with Win32 1307
// (ERROR_INVALID_OWNER) on a SetNamedSecurityInfo. Enabling these
// here is inherited by every CreateProcess child the agent spawns
// (download_install, run_script, console_open, ...), so installers
// and pnputil calls Just Work.
//
// We don't fail the agent if a privilege isn't present (running as
// non-admin during dev is fine — only the install paths will fail).
void enable_install_privileges() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(),
                              TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        ::lsw::agent::log::append(
            ::lsw::agent::log::Level::Warn,
            std::string{"privileges: OpenProcessToken failed err="}
                + std::to_string(::GetLastError()));
        return;
    }
    static constexpr const wchar_t* kPrivs[] = {
        SE_TAKE_OWNERSHIP_NAME,  // ACL ownership changes (the 1307 fix)
        SE_RESTORE_NAME,         // restoring files with original ownership
        SE_BACKUP_NAME,          // reading files past their DACL
        SE_SECURITY_NAME,         // SACL / audit access
        SE_LOAD_DRIVER_NAME,     // driver install (DIFx, pnputil)
        SE_DEBUG_NAME,           // some installers cross-poke service processes
    };
    int enabled = 0;
    for (const auto* name : kPrivs) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        if (!::LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
            continue;
        }
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (::AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr)
            && ::GetLastError() == ERROR_SUCCESS) {
            ++enabled;
        }
    }
    ::CloseHandle(token);
    ::lsw::agent::log::append(
        ::lsw::agent::log::Level::Info,
        std::string{"privileges: enabled "} + std::to_string(enabled)
            + " of " + std::to_string(sizeof(kPrivs) / sizeof(kPrivs[0]))
            + " install-relevant token privileges");
}

}  // namespace

int wmain(int /*argc*/, wchar_t** /*argv*/) {
    ::lsw::agent::log::append(::lsw::agent::log::Level::Info,
                               "agent: process start");
    // No install-sentinel gate: the agent autostarts from HKLM\Run,
    // which the shell processes only at the real post-OOBE
    // interactive logon — nothing to wait for. Admin promotion is
    // done by LswPromote.ps1 (SetupComplete), not here.
    enable_install_privileges();
    return ::lsw::agent::core::AgentService::instance().run();
}
