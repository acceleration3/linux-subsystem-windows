#include "lsw/host/mcp/tools/raw_rpc_tool.hpp"

#include "lsw/host/mcp/agent_command.hpp"
#include "lsw/host/mcp/json_schema.hpp"
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
    const auto cmd = args.value("cmd", std::string{});
    if (cmd.empty()) {
        return ToolResult::error("rpc: missing cmd");
    }
    const auto params = (args.contains("params") && args["params"].is_object())
                            ? args["params"]
                            : nlohmann::json::object();
    const auto r = agent_.invoke(cmd, params);
    return r.ok ? ToolResult::json_body(r.body) : ToolResult::error(r.error);
}

}  // namespace lsw::host::mcp
