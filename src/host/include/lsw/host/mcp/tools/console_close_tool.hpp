#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;
class ConsoleSessionBuffer;

// Terminates a console session in the guest and drops its host-side
// buffer, so a reused session id cannot return a dead session's output.
class ConsoleCloseTool final : public IMcpTool {
public:
    ConsoleCloseTool(AgentCommand& agent, ConsoleSessionBuffer& buffer) noexcept
        : agent_(agent), buffer_(buffer) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
    ConsoleSessionBuffer& buffer_;
};

}  // namespace lsw::host::mcp
