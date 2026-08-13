#include "lsw/host/mcp/json_schema.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

nlohmann::json JsonSchema::no_args() {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}

nlohmann::json JsonSchema::object(const nlohmann::json& properties,
                                  const std::vector<std::string>& required) {
    nlohmann::json s = {{"type", "object"}, {"properties", properties}};
    if (!required.empty()) {
        nlohmann::json req = nlohmann::json::array();
        for (const auto& r : required) {
            req.push_back(r);
        }
        s["required"] = std::move(req);
    }
    return s;
}

}  // namespace lsw::host::mcp
