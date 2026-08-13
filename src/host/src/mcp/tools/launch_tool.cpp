#include "lsw/host/mcp/tools/launch_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string LaunchTool::name() const { return "launch"; }

std::string LaunchTool::description() const {
    return "Spawn an app in the user session. ``app_id`` is an absolute path to "
           "an .exe or .lnk (use ``list_apps`` to find one). Returns the spawned "
           "PID — feed that to ``list_windows`` filtering to find the new window.";
}

nlohmann::json LaunchTool::input_schema() const {
    return JsonSchema::object({{"app_id", {{"type", "string"}}}}, {"app_id"});
}

nlohmann::json LaunchTool::call(const nlohmann::json& args) {
    const auto app_id = args.value("app_id", std::string{});
    if (app_id.empty()) {
        return ToolResult::error("launch: missing app_id");
    }
    const auto r = agent_.invoke(linkproto::cmd::kLaunch, {{"app_id", app_id}});
    return r.ok ? ToolResult::json_body(r.body) : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
