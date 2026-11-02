#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::mcp {
class JsonRpcFrame {
public:
    JsonRpcFrame() = delete;

    static constexpr int kParseError     = -32700;
    static constexpr int kInvalidRequest = -32600;
    static constexpr int kMethodNotFound = -32601;
    static constexpr int kInvalidParams  = -32602;
    static constexpr int kInternalError  = -32603;

    [[nodiscard]] static nlohmann::json error(const nlohmann::json& id, int code,
                                              const std::string& message);

    [[nodiscard]] static nlohmann::json result(const nlohmann::json& id,
                                               nlohmann::json result);
};

}
