#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class ConsoleSessionBuffer;

class ConsoleReadTool final : public IMcpTool {
public:
    explicit ConsoleReadTool(ConsoleSessionBuffer& buffer) noexcept : buffer_(buffer) {}

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] nlohmann::json input_schema() const override;
    [[nodiscard]] nlohmann::json call(const nlohmann::json& args) override;

private:
    ConsoleSessionBuffer& buffer_;
};

}
