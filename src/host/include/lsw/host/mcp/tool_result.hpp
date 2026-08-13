#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::mcp {

// Builds the ``result`` body MCP expects back from ``tools/call``: a
// single text content block plus an ``isError`` flag.
//
// Its own class so no tool has to remember the wire shape — they return
// meaning, this turns it into protocol.
class ToolResult {
public:
    ToolResult() = delete;

    // Plain text block. ``is_error`` makes the client surface it as a
    // failed call rather than a normal answer.
    [[nodiscard]] static nlohmann::json text(const std::string& text, bool is_error = false);

    // Pretty-printed JSON in a text block — what most tools return.
    [[nodiscard]] static nlohmann::json json_body(const nlohmann::json& payload);

    // Failure, with a message the model is meant to read and act on.
    [[nodiscard]] static nlohmann::json error(const std::string& message);
};

}  // namespace lsw::host::mcp
