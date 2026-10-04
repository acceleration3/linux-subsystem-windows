#include "lsw/host/mcp/tools/raw_rpc_tool.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
#include "lsw/host/mcp/tool_result.hpp"

namespace lsw::host::mcp {

std::string RawRpcTool::name() const { return "rpc"; }

std::string RawRpcTool::description() const {
    return "Escape hatch: forward an arbitrary RPC frame to the agent. ``cmd`` is "
           "the verb (e.g. ``close_window``), ``params`` is the args object. "
           "Returns the raw reply body. Use this for verbs not exposed as a "
           "dedicated tool.";
}

nlohmann::json RawRpcTool::input_schema() const {
    return JsonSchema::object({
        {"cmd", {{"type", "string"}}},
        {"params", {{"type", "object"}}},
    }, {"cmd"});
}

nlohmann::json RawRpcTool::call(const nlohmann::json& args) {
    const ToolArgs in{name(), args};
    const auto cmd = in.string("cmd");
    if (cmd.empty()) {
        return in.missing("cmd");
    }
    return ToolResult::from_reply(agent_.request(cmd, in.object("params")));
}

}  // namespace lsw::host::mcp
