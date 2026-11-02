#pragma once

#include <string>

namespace lsw::agent::util {

class PowerShellCommand {
public:
    PowerShellCommand() = delete;

    [[nodiscard]] static std::wstring encoded(const std::string& script_utf8);
};

}
