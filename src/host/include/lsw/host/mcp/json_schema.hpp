#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace lsw::host::mcp {

// The two JSON Schema shapes every tool's ``inputSchema`` needs. Tools
// describe their arguments through this rather than hand-writing schema
// objects, so the schemas cannot drift apart in style.
class JsonSchema {
public:
    JsonSchema() = delete;

    // ``{"type":"object","properties":{}}`` — a tool that takes nothing.
    [[nodiscard]] static nlohmann::json no_args();

    // An object schema over ``properties``, with an optional required list.
    [[nodiscard]] static nlohmann::json object(const nlohmann::json& properties,
                                               const std::vector<std::string>& required = {});
};

}  // namespace lsw::host::mcp
