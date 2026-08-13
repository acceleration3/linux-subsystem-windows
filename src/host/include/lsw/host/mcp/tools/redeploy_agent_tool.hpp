#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Exposes AgentRedeployer as a tool. Thin on purpose — the swap
// sequence is the redeployer's job, not the protocol layer's.
class RedeployAgentTool final : public IMcpTool {
public:
    explicit RedeployAgentTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
