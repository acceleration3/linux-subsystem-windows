#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

nlohmann::json ToolResult::text(const std::string& text, bool is_error) {
    return {
        {"content", nlohmann::json::array({{{"type", "text"}, {"text", text}}})},
        {"isError", is_error},
    };
}

nlohmann::json ToolResult::json_body(const nlohmann::json& payload) {
    return text(payload.dump(2));
}

nlohmann::json ToolResult::error(const std::string& message) {
    return text(message, /*is_error=*/true);
}

}  // namespace lsw::host::mcp
