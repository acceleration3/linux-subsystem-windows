#pragma once

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::mcp {

class AgentCommand;
class ToolRegistry;

// Turns a JSON-RPC frame into the right MCP response.
//
// Knows the MCP method set (``initialize``, ``tools/list``,
// ``tools/call``, and the harmless probes clients send) and nothing
// about HTTP or about what any individual tool does.
class JsonRpcRouter {
public:
    JsonRpcRouter(ToolRegistry& tools, AgentCommand& agent) noexcept
        : tools_(tools), agent_(agent) {}

    // Never throws: a tool that throws becomes an internal-error reply.
    [[nodiscard]] nlohmann::json dispatch(const nlohmann::json& request);

private:
    [[nodiscard]] static nlohmann::json on_initialize();
    [[nodiscard]] nlohmann::json on_tools_list() const;
    [[nodiscard]] nlohmann::json on_tools_call(const nlohmann::json& params);

    ToolRegistry& tools_;
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
