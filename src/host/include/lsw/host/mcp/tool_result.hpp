#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::link { struct AgentReply; }

namespace lsw::host::mcp {
class ToolResult {
public:
    ToolResult() = delete;

    [[nodiscard]] static nlohmann::json text(const std::string& text, bool is_error = false);

    [[nodiscard]] static nlohmann::json json_body(const nlohmann::json& payload);

    [[nodiscard]] static nlohmann::json error(const std::string& message);

    [[nodiscard]] static nlohmann::json from_reply(const link::AgentReply& reply);

    [[nodiscard]] static nlohmann::json acknowledge(const link::AgentReply& reply,
                                                    const std::string& ok_text = "ok");
};

}
