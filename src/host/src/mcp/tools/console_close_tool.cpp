#include "lsw/host/mcp/tools/console_close_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/console_session_buffer.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
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
    const ToolArgs in{name(), args};
    const auto session_id = in.string("session_id");
    if (session_id.empty()) {
        return in.missing("session_id");
    }
    const auto r = agent_.request(linkproto::cmd::kConsoleClose, {{"session_id", session_id}});
    // Forget the buffer either way: if the close failed the session is in
    // an unknown state, and stale bytes would be worse than none.
    buffer_.forget(QString::fromStdString(session_id));
    return ToolResult::acknowledge(r);
}

}  // namespace lsw::host::mcp
