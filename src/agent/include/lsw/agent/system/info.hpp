#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::system {

// Reports OS version, RAM, CPU load, disk, hostname, primary IP and the
// agent's build stamp, as the ``system_info`` reply object.
class Info {
public:
    [[nodiscard]] static nlohmann::json collect();
};

}  // namespace lsw::agent::system
