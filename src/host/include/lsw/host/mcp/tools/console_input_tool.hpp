#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class AgentCommand;

// Writes a line to a console session's stdin. Line breaks are the
// caller's responsibility: the bytes are passed through verbatim.
class ConsoleInputTool final : public IMcpTool {
public:
    explicit ConsoleInputTool(AgentCommand& agent) noexcept : agent_(agent) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    AgentCommand& agent_;
};

}  // namespace lsw::host::mcp
