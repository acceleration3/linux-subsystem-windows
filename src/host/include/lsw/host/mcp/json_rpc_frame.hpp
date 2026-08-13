#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::mcp {

// JSON-RPC 2.0 envelope construction and its error codes. The router
// decides what happened; this decides how to say it on the wire.
class JsonRpcFrame {
public:
    JsonRpcFrame() = delete;

    // JSON-RPC 2.0 reserved codes, plus the MCP-flavoured usage of them.
    static constexpr int kParseError     = -32700;
    static constexpr int kInvalidRequest = -32600;
    static constexpr int kMethodNotFound = -32601;
    static constexpr int kInvalidParams  = -32602;
    static constexpr int kInternalError  = -32603;

    // Error reply. A null ``id`` is preserved as null, per spec, for
    // failures that happened before the id could be read.
    [[nodiscard]] static nlohmann::json error(const nlohmann::json& id, int code,
                                              const std::string& message);

    // Success reply carrying ``result``.
    [[nodiscard]] static nlohmann::json result(const nlohmann::json& id,
                                               nlohmann::json result);
};

}  // namespace lsw::host::mcp
