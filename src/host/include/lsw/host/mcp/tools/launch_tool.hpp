#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Spawns an app in the guest user session and returns its PID, which
// is what makes the new window findable in ``list_windows``.
class LaunchTool final : public IMcpTool {
public:
    explicit LaunchTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
