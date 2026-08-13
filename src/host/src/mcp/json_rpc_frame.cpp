#include "lsw/host/mcp/json_rpc_frame.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

nlohmann::json JsonRpcFrame::error(const nlohmann::json& id, int code,
                                   const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id.is_null() ? nlohmann::json() : id},
        {"error", {{"code", code}, {"message", message}}},
    };
}

nlohmann::json JsonRpcFrame::result(const nlohmann::json& id, nlohmann::json result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    };
}

}  // namespace lsw::host::mcp
