#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Enumerates apps installed in the guest. Strips the base64 icons out
// of the agent reply — they are megabytes of noise to a model.
class ListAppsTool final : public IMcpTool {
public:
    explicit ListAppsTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
