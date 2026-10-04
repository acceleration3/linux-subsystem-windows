#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

// Exposes AgentRedeployer as a tool. Thin on purpose — the swap
// sequence is the redeployer's job, not the protocol layer's.
class RedeployAgentTool final : public AgentTool {
public:
    using AgentTool::AgentTool;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
