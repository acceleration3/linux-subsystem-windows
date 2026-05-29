// lsw-guest-agent entry point.
//
// Autostarted from the ``lsw-agent`` scheduled task (registered by
// LswPromote.ps1 at SetupComplete, with LogonTrigger -User <wizard
// SID>, LogonType InteractiveToken, RunLevel Highest). The task
// fires the instant LSA mints the user's session token — which is
// EARLIER than the shell coming up — so before binding our TCP
// listener we wait on the ``C:\ProgramData\LSW\lsw-ready.flag``
// sentinel that FirstLogon.ps1 writes as its last action. Without
// the gate the manager's PostInstallWatcher sees ``:5102`` alive
// while Windows is still on the OOBE "preparing your account"
// screens and prematurely declares install done. The wizard
// account is a local admin and the agent EXE is manifested as
// requireAdministrator, so children inherit a full token for
// pnputil / sc / driver work.

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
    // Wait for the OOBE-done sentinel before going further. Bounded
    // so a partially-broken install (FirstLogon never ran) still
    // gives up and starts the agent after 30 minutes — better to be
    // reachable late than not at all. Once written the flag survives
    // forever, so reboots and redeploy-swaps fall straight through.
    constexpr const wchar_t* kReadyFlag =
        L"C:\\ProgramData\\LSW\\lsw-ready.flag";
    if (::GetFileAttributesW(kReadyFlag) == INVALID_FILE_ATTRIBUTES) {
        ::lsw::agent::log::append(
            ::lsw::agent::log::Level::Info,
            "agent: waiting for lsw-ready.flag (OOBE-done sentinel)");
        for (int i = 0; i < 1800; ++i) {
            ::Sleep(1000);
            if (::GetFileAttributesW(kReadyFlag) != INVALID_FILE_ATTRIBUTES) {
                ::lsw::agent::log::append(
                    ::lsw::agent::log::Level::Info,
                    std::string{"agent: sentinel present after "}
                        + std::to_string(i + 1) + "s");
                break;
            }
        }
        if (::GetFileAttributesW(kReadyFlag) == INVALID_FILE_ATTRIBUTES) {
            ::lsw::agent::log::append(
                ::lsw::agent::log::Level::Warn,
                "agent: sentinel never appeared after 30 min — proceeding anyway");
        }
    }
    enable_install_privileges();
    return ::lsw::agent::core::AgentService::instance().run();
}
