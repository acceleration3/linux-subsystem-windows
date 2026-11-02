#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::services {
class GuestUpdate;
class StatusHub;
}

namespace lsw::host::mcp {

// Exposes GuestUpdate as a tool: the manager's own update of Windows, run
// and waited for. Thin on purpose; the run is GuestUpdate's.
class GuestUpdateTool final : public IMcpTool {
public:
    GuestUpdateTool(services::GuestUpdate& update, services::StatusHub& status) noexcept
        : update_(update), status_(status) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    services::GuestUpdate& update_;
    services::StatusHub& status_;
};

}  // namespace lsw::host::mcp
