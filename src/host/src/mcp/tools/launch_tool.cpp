#include "lsw/host/mcp/tools/launch_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string LaunchTool::name() const { return "launch"; }

std::string LaunchTool::description() const {
    return "Spawn an app in the user session. ``app_id`` is an absolute path to "
           "an .exe or .lnk (use ``list_apps`` to find one). Returns the spawned "
           "PID; feed that to ``list_windows`` filtering to find the new window. "
           "``session`` picks a Windows session other than the console's (see the "
           "``list_sessions`` RPC); ``session: 0`` starts it as SYSTEM in session 0.";
}

nlohmann::json LaunchTool::input_schema() const {
    return JsonSchema::object({{"app_id", {{"type", "string"}}},
                               {"session", {{"type", "integer"}, {"minimum", 0}}}},
                              {"app_id"});
}

nlohmann::json LaunchTool::call(const nlohmann::json& args) {
    const ToolArgs in{name(), args};
    const auto app_id = in.string("app_id");
    if (app_id.empty()) {
        return in.missing("app_id");
    }
    nlohmann::json params = {{"app_id", app_id}};
    if (const auto session = in.integer("session", -1); session >= 0) {
        params["session"] = session;
    }
    return ToolResult::from_reply(agent_.request(linkproto::cmd::kLaunch, params));
}

}  // namespace lsw::host::mcp
