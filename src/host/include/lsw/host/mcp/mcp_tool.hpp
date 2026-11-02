#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::mcp {
class IMcpTool {
public:
    IMcpTool() = default;
    virtual ~IMcpTool() = default;
    IMcpTool(const IMcpTool&) = delete;
    IMcpTool& operator=(const IMcpTool&) = delete;

    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual std::string description() const = 0;

    [[nodiscard]] virtual nlohmann::json input_schema() const = 0;

    [[nodiscard]] virtual bool requires_agent() const { return true; }

    [[nodiscard]] virtual nlohmann::json call(const nlohmann::json& args) = 0;
};

}
