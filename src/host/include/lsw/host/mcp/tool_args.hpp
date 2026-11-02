#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace lsw::host::mcp {
class ToolArgs {
public:
    ToolArgs(std::string_view tool, const nlohmann::json& args) : tool_(tool), args_(args) {}

    [[nodiscard]] std::string string(std::string_view key) const;

    [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t fallback) const;

    [[nodiscard]] nlohmann::json object(std::string_view key) const;

    [[nodiscard]] nlohmann::json missing(std::string_view key) const;

private:
    [[nodiscard]] const nlohmann::json* find(std::string_view key) const;

    std::string_view tool_;
    const nlohmann::json& args_;
};

}
