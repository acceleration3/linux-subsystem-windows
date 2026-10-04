#pragma once

#include "lsw/host/mcp/agent_tool.hpp"

namespace lsw::host::mcp {

// Writes a line to a console session's stdin. Line breaks are the
// caller's responsibility: the bytes are passed through verbatim.
class ConsoleInputTool final : public AgentTool {
public:
    using AgentTool::AgentTool;

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
