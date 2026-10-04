#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

// Spawns a cmd.exe in the guest and returns its session id — the
// handle the other three console tools take.
class ConsoleOpenTool final : public AgentTool {
public:
    using AgentTool::AgentTool;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
