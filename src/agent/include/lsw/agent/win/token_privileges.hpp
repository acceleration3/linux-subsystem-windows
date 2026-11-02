#pragma once

namespace lsw::agent::win {
class TokenPrivileges {
public:
    TokenPrivileges() = delete;

    static int enable_install_privileges(void* token = nullptr);

    static bool enable(const wchar_t* name);
};

}
