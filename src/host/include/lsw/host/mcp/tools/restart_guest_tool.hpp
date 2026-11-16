#pragma once

#include "lsw/host/manager/guest_restart.hpp"
#include "lsw/host/mcp/mcp_tool.hpp"

#include <QString>

#include <functional>

class QObject;

namespace lsw::host::mcp {
class RestartGuestTool final : public IMcpTool {
public:
    using Restarter = std::function<manager::RestartResult(QString*)>;

    RestartGuestTool(QObject& main_thread, Restarter restarter)
        : main_thread_(main_thread), restarter_(std::move(restarter)) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    QObject& main_thread_;
    Restarter restarter_;
};

}
