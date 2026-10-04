#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

class ConsoleSessionBuffer;

// Terminates a console session in the guest and drops its host-side
// buffer, so a reused session id cannot return a dead session's output.
class ConsoleCloseTool final : public AgentTool {
public:
    ConsoleCloseTool(link::AgentRpcClient& agent, ConsoleSessionBuffer& buffer) noexcept
        : AgentTool(agent), buffer_(buffer) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    ConsoleSessionBuffer& buffer_;
};

}  // namespace lsw::host::mcp
