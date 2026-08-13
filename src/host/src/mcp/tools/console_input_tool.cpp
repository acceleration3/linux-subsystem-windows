#include "lsw/host/mcp/tools/console_input_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ConsoleInputTool::name() const { return "console_input"; }

std::string ConsoleInputTool::description() const {
    return "Send a line to cmd.exe stdin. ``data`` is the raw text; if it doesn't "
           "end with ``\\r\\n`` you'll need to send the line break separately. "
           "Typical pattern: send ``cd C:\\Users\\\\r\\n``, then ``console_read``.";
}

nlohmann::json ConsoleInputTool::input_schema() const {
    return JsonSchema::object({
        {"session_id", {{"type", "string"}}},
        {"data", {{"type", "string"}}},
    }, {"session_id", "data"});
}

nlohmann::json ConsoleInputTool::call(const nlohmann::json& args) {
    const auto session_id = args.value("session_id", std::string{});
    if (session_id.empty()) {
        return ToolResult::error("console_input: missing session_id");
    }
    const auto r = agent_.invoke(linkproto::cmd::kConsoleInput, {
        {"session_id", session_id},
        {"data", args.value("data", std::string{})},
    });
    return r.ok ? ToolResult::text("ok") : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
