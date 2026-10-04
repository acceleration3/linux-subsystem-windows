#include "lsw/host/mcp/tools/console_open_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ConsoleOpenTool::name() const { return "console_open"; }

std::string ConsoleOpenTool::description() const {
    return "Spawn a fresh cmd.exe in the VM and return its ``session_id``. Use "
           "``console_input`` to send commands, ``console_read`` to poll output, "
           "``console_close`` to terminate. By default it runs as the logged-on user "
           "(elevated) in the console session; ``session`` picks another Windows "
           "session (see the ``list_sessions`` RPC), and ``session: 0`` runs it as "
           "SYSTEM in session 0.";
}

nlohmann::json ConsoleOpenTool::input_schema() const {
    return JsonSchema::object({{"session", {{"type", "integer"}, {"minimum", 0}}}});
}

nlohmann::json ConsoleOpenTool::call(const nlohmann::json& args) {
    const ToolArgs in{name(), args};
    nlohmann::json params = nlohmann::json::object();
    if (const auto session = in.integer("session", -1); session >= 0) {
        params["session"] = session;
    }
    const auto r = agent_.request(linkproto::cmd::kConsoleOpen, params);
    if (!r.ok) {
        return ToolResult::error(r.error);
    }
    const auto session_id = r.body.value("session_id", std::string{});
    if (session_id.empty()) {
        return ToolResult::error("agent returned no session_id");
    }
    std::string text = "session_id=" + session_id;
    if (r.body.contains("session")) {
        text += " (Windows session " + std::to_string(r.body.value("session", 0)) + ", " +
                r.body.value("user", std::string{"?"}) +
                (r.body.value("elevated", false) ? ", elevated)" : ")");
    }
    return ToolResult::text(text);
}

}  // namespace lsw::host::mcp
