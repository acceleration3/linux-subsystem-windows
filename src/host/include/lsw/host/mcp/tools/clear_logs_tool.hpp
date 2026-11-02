#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::services { class LogCleaner; }

namespace lsw::host::mcp {

class ClearLogsTool final : public IMcpTool {
public:
    explicit ClearLogsTool(services::LogCleaner& cleaner) noexcept : cleaner_(cleaner) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    services::LogCleaner& cleaner_;
};

}
