#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::system {

class LogPacker {
public:
    LogPacker() = delete;

    [[nodiscard]] static nlohmann::json pack();
};

}
