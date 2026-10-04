#include "lsw/host/mcp/tools/list_apps_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

std::string ListAppsTool::name() const { return "list_apps"; }

std::string ListAppsTool::description() const {
    return "Enumerate apps installed in the VM (Add/Remove Programs view). "
           "Returns a JSON array of {id, name, exe_path}. Slow first call (a few "
           "hundred ms for icon extraction).";
}

nlohmann::json ListAppsTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json ListAppsTool::call(const nlohmann::json&) {
    const auto r = agent_.request(linkproto::cmd::kListApps);
    if (!r.ok) {
        return ToolResult::error(r.error);
    }
    // Drop the icons: megabytes of base64 that a model can do nothing with.
    nlohmann::json out = r.body;
    if (out.contains("apps") && out["apps"].is_array()) {
        for (auto& app : out["apps"]) {
            if (app.is_object()) {
                app.erase("icon_png_base64");
            }
        }
    }
    return ToolResult::json_body(out);
}

}  // namespace lsw::host::mcp
