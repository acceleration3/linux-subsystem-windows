#include "lsw/agent/win/token_privileges.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <string>
#include <windows.h>

namespace lsw::agent::win {

namespace {

constexpr const wchar_t* kInstallPrivileges[] = {
    SE_TAKE_OWNERSHIP_NAME,  // ACL ownership changes (the 1307 fix)
    SE_RESTORE_NAME,         // restoring files with original ownership
    SE_BACKUP_NAME,          // reading files past their DACL
    SE_SECURITY_NAME,        // SACL / audit access
    SE_LOAD_DRIVER_NAME,     // driver install (DIFx, pnputil)
    SE_DEBUG_NAME,           // some installers cross-poke service processes
};

constexpr int kPrivilegeCount =
    static_cast<int>(sizeof(kInstallPrivileges) / sizeof(kInstallPrivileges[0]));

}  // namespace

int TokenPrivileges::enable_install_privileges() {
    UniqueHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                            token.put())) {
        LSW_AGENT_WARN("privileges: OpenProcessToken failed err={}", ErrorText::last_error());
        return 0;
    }
    int enabled = 0;
    for (const auto* name : kInstallPrivileges) {
        TOKEN_PRIVILEGES privilege{};
        privilege.PrivilegeCount = 1;
        if (!::LookupPrivilegeValueW(nullptr, name, &privilege.Privileges[0].Luid)) {
            continue;
        }
        privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (::AdjustTokenPrivileges(token.get(), FALSE, &privilege, sizeof(privilege), nullptr,
                                    nullptr) &&
            ::GetLastError() == ERROR_SUCCESS) {
            ++enabled;
        }
    }
    LSW_AGENT_INFO("privileges: enabled {} of {} install-relevant token privileges", enabled,
                   kPrivilegeCount);
    return enabled;
}

}  // namespace lsw::agent::win
