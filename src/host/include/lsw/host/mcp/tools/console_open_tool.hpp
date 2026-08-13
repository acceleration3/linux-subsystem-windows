#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Spawns a cmd.exe in the guest and returns its session id — the
// handle the other three console tools take.
class ConsoleOpenTool final : public IMcpTool {
public:
    explicit ConsoleOpenTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
