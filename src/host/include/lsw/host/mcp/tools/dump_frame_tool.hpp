#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

// Writes the last frame a mirror received out as a PNG and reports the
// geometry that went with it — the mirror-debugging tool.
//
// Needs no agent: it reads what the manager already holds, so it still
// answers when the guest is wedged.
class DumpFrameTool final : public IMcpTool {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] bool requires_agent() const override { return false; }
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;
};

}  // namespace lsw::host::mcp
