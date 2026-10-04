#include "lsw/host/mcp/tools/console_input_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
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
    const ToolArgs in{name(), args};
    const auto session_id = in.string("session_id");
    if (session_id.empty()) {
        return in.missing("session_id");
    }
    const auto r = agent_.request(linkproto::cmd::kConsoleInput, {
        {"session_id", session_id},
        {"data", in.string("data")},
    });
    return ToolResult::acknowledge(r);
}

}  // namespace lsw::host::mcp
