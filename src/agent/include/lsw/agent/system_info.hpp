#pragma once

#include <string>

namespace lsw::agent {

// Reports OS version, RAM, disk, hostname, primary IP. Result is a
// JSON object literal (no fancy formatting; the host parses with Qt).
class SystemInfo {
public:
    [[nodiscard]] static std::string collect_json();
};

}  // namespace lsw::agent
