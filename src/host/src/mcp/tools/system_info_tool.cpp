#include "lsw/host/mcp/tools/system_info_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string SystemInfoTool::name() const { return "system_info"; }

std::string SystemInfoTool::description() const {
    return "Returns the guest VM's OS version, hostname, IPv4, total/available RAM. "
           "No arguments. Use this first to confirm the agent is reachable.";
}

nlohmann::json SystemInfoTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json SystemInfoTool::call(const nlohmann::json&) {
    const auto r = agent_.invoke(linkproto::cmd::kSystemInfo);
    return r.ok ? ToolResult::json_body(r.body) : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
