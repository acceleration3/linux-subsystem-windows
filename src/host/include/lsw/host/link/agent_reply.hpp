#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::host::link {
struct AgentReply {
    bool ok = false;

    nlohmann::json body;

    std::string error;

    [[nodiscard]] static AgentReply failure(std::string why);

    [[nodiscard]] static AgentReply from_frame(const nlohmann::json& frame);
};

}
