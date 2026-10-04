#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

// Spawns an app in the guest user session and returns its PID, which
// is what makes the new window findable in ``list_windows``.
class LaunchTool final : public AgentTool {
public:
    using AgentTool::AgentTool;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
