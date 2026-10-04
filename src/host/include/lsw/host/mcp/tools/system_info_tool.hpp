#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

// Reports the guest OS version, hostname, IPv4 and memory. The
// cheapest possible round trip, so it doubles as the reachability
// check every session starts with.
class SystemInfoTool final : public AgentTool {
public:
    using AgentTool::AgentTool;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
