#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::apps { class LogStore; }

namespace lsw::host::mcp {
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

}
