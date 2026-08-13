#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

namespace lsw::host::mcp {

class ConsoleSessionBuffer;

// Drains the output a console session has produced since the last read.
//
// The only console tool that does not talk to the guest: the bytes were
// pushed to the manager already and are sitting in the buffer.
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

}  // namespace lsw::host::mcp
