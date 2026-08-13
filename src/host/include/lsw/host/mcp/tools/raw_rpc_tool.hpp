#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Escape hatch: forwards an arbitrary RPC frame to the agent. Exists so
// a verb without a dedicated tool is still reachable, which is what
// keeps the tool list small rather than one-per-verb.
class RawRpcTool final : public IMcpTool {
public:
    explicit RawRpcTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
