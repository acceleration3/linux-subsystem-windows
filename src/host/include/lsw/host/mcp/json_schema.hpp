#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace lsw::host::mcp {
class JsonSchema {
public:
    JsonSchema() = delete;

    [[nodiscard]] static nlohmann::json no_args();

    [[nodiscard]] static nlohmann::json object(const nlohmann::json& properties,
                                               const std::vector<std::string>& required = {});
};

}
