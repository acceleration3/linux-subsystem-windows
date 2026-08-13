#include "lsw/host/mcp/tools/console_close_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/console_session_buffer.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <QString>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ConsoleCloseTool::name() const { return "console_close"; }

std::string ConsoleCloseTool::description() const {
    return "Terminate the cmd.exe session.";
}

nlohmann::json ConsoleCloseTool::input_schema() const {
    return JsonSchema::object({{"session_id", {{"type", "string"}}}}, {"session_id"});
}

nlohmann::json ConsoleCloseTool::call(const nlohmann::json& args) {
    const auto session_id = args.value("session_id", std::string{});
    if (session_id.empty()) {
        return ToolResult::error("console_close: missing session_id");
    }
    const auto r = agent_.invoke(linkproto::cmd::kConsoleClose, {{"session_id", session_id}});
    // Forget the buffer either way: if the close failed the session is in
    // an unknown state, and stale bytes would be worse than none.
    buffer_.forget(QString::fromStdString(session_id));
    return r.ok ? ToolResult::text("ok") : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
