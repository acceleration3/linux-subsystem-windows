#pragma once

#include <string>

namespace lsw::agent::system {

// Reports OS version, RAM, disk, hostname, primary IP. Result is a
// JSON object literal (no fancy formatting; the host parses with Qt).
class Info {
public:
    [[nodiscard]] static std::string collect_json();
};

}  // namespace lsw::agent::system
