#include "lsw/host/mcp/tools/console_open_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ConsoleOpenTool::name() const { return "console_open"; }

std::string ConsoleOpenTool::description() const {
    return "Spawn a fresh cmd.exe in the VM and return its ``session_id``. Use "
           "``console_input`` to send commands, ``console_read`` to poll output, "
           "``console_close`` to terminate.";
}

nlohmann::json ConsoleOpenTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json ConsoleOpenTool::call(const nlohmann::json&) {
    const auto r = agent_.invoke(linkproto::cmd::kConsoleOpen);
    if (!r.ok) {
        return ToolResult::error(r.error);
    }
    const auto session_id = r.body.value("session_id", std::string{});
    if (session_id.empty()) {
        return ToolResult::error("agent returned no session_id");
    }
    return ToolResult::text("session_id=" + session_id);
}

}  // namespace lsw::host::mcp
