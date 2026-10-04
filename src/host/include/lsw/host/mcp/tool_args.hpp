#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace lsw::host::mcp {

// Typed reads of a tool's ``arguments`` object.
//
// A model sends what it sends: a number where a string belongs, an
// array where an object does. Reading with ``json::value`` throws on
// that, and the throw surfaced as a JSON-RPC internal error — which a
// model reads as "the server is broken", not "fix your argument". Here a
// value of the wrong type reads as absent, and ``missing`` turns that
// into a tool error the model can act on.
class ToolArgs {
public:
    // ``tool`` prefixes every error, so the model knows which call it was.
    ToolArgs(std::string_view tool, const nlohmann::json& args) : tool_(tool), args_(args) {}

    // The string at ``key``; empty when absent or not a string.
    [[nodiscard]] std::string string(std::string_view key) const;

    // The integer at ``key``; ``fallback`` when absent or not an integer.
    [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t fallback) const;

    // The object at ``key``; an empty object when absent or not an object.
    [[nodiscard]] nlohmann::json object(std::string_view key) const;

    // The tool error for a required ``key`` that was absent or unusable.
    [[nodiscard]] nlohmann::json missing(std::string_view key) const;

private:
    [[nodiscard]] const nlohmann::json* find(std::string_view key) const;

    std::string_view tool_;
    const nlohmann::json& args_;
};

}  // namespace lsw::host::mcp
