#include "lsw/host/mcp/tools/list_windows_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ListWindowsTool::name() const { return "list_windows"; }

std::string ListWindowsTool::description() const {
    return "Enumerate top-level visible windows in the VM. Returns "
           "{hwnd, pid, title, exe_path, client_w, client_h} per window.";
}

nlohmann::json ListWindowsTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json ListWindowsTool::call(const nlohmann::json&) {
    const auto r = agent_.invoke(linkproto::cmd::kListWindows);
    return r.ok ? ToolResult::json_body(r.body) : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
