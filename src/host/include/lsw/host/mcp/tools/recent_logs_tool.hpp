#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::apps { class LogStore; }

namespace lsw::host::mcp {

// Returns the tail of the agent log the manager has already collected.
//
// Deliberately usable with no VM targeted: when the agent is unreachable
// the log is often the only thing that explains why.
class RecentLogsTool final : public IMcpTool {
public:
    explicit RecentLogsTool(apps::LogStore& log_store) noexcept : log_store_(log_store) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] bool requires_agent() const override { return false; }
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    apps::LogStore& log_store_;
};

}  // namespace lsw::host::mcp
