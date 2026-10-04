#include "lsw/host/mcp/json_rpc_router.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/json_rpc_frame.hpp"
#include "lsw/host/mcp/tool_registry.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

nlohmann::json JsonRpcRouter::dispatch(const nlohmann::json& request) {
    if (!request.is_object() || request.value("jsonrpc", std::string{}) != "2.0") {
        return JsonRpcFrame::error(request.value("id", nlohmann::json()),
                                   JsonRpcFrame::kInvalidRequest,
                                   "not a JSON-RPC 2.0 frame");
    }
    const auto id = request.value("id", nlohmann::json());
    const auto method = request.value("method", std::string{});
    const auto params = (request.contains("params") && request["params"].is_object())
                            ? request["params"]
                            : nlohmann::json::object();

    try {
        if (method == "initialize") return JsonRpcFrame::result(id, on_initialize());
        if (method == "tools/list") return JsonRpcFrame::result(id, on_tools_list());
        if (method == "tools/call") return JsonRpcFrame::result(id, on_tools_call(params));
        // Harmless probes clients send on connect — answer empty so they
        // move on rather than treating us as broken.
        if (method == "notifications/initialized") return JsonRpcFrame::result(id, {});
        if (method == "resources/list") {
            return JsonRpcFrame::result(id, {{"resources", nlohmann::json::array()}});
        }
        if (method == "prompts/list") {
            return JsonRpcFrame::result(id, {{"prompts", nlohmann::json::array()}});
        }
        if (method == "ping") return JsonRpcFrame::result(id, {});
        return JsonRpcFrame::error(id, JsonRpcFrame::kMethodNotFound,
                                   "unknown method: " + method);
    } catch (const std::exception& e) {
        return JsonRpcFrame::error(id, JsonRpcFrame::kInternalError, e.what());
    }
}

nlohmann::json JsonRpcRouter::on_initialize() {
    return {
        {"protocolVersion", "2024-11-05"},
        {"serverInfo", {{"name", "lsw-manager"}, {"version", "0.0.1"}}},
        {"capabilities", {{"tools", nlohmann::json::object()}}},
    };
}

nlohmann::json JsonRpcRouter::on_tools_list() const {
    return {{"tools", tools_.describe_all()}};
}

nlohmann::json JsonRpcRouter::on_tools_call(const nlohmann::json& params) {
    const auto name = params.value("name", std::string{});
    const auto args = (params.contains("arguments") && params["arguments"].is_object())
                          ? params["arguments"]
                          : nlohmann::json::object();

    auto* tool = tools_.find(name);
    if (tool == nullptr) {
        return ToolResult::error("unknown tool: " + name);
    }
    if (tool->requires_agent() && !agent_.has_target()) {
        return ToolResult::error("No VM is currently targeted, so the agent cannot be reached.");
    }
    return tool->call(args);
}

}  // namespace lsw::host::mcp
