#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::services { class LogCollector; }

namespace lsw::host::mcp {

class CollectLogsTool final : public IMcpTool {
public:
    explicit CollectLogsTool(services::LogCollector& collector) noexcept : collector_(collector) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    services::LogCollector& collector_;
};

}
