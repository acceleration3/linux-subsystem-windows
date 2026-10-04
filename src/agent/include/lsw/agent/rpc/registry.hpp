#pragma once

#include "lsw/agent/rpc/handler.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lsw::agent::rpc {

// Process-wide map from command name to handler instance. Populated
// at static-init time via ``LSW_REGISTER_RPC``; the dispatcher in
// transport just does a single lookup.
//
// Meyers-singleton instance: safe from the static-init-order fiasco
// because the registry is constructed on first ``instance()`` call,
// which is exactly when the first ``AutoRegister`` global runs.
class RpcRegistry {
public:
    static RpcRegistry& instance();

    // Insert a handler. ABORTS on duplicate name (logged + abort) —
    // duplicates are always a coding error, never a runtime
    // condition.
    void register_handler(std::string name, std::unique_ptr<RpcHandler> h);

    // Look up by command name. Returns nullptr if no handler is
    // registered.
    [[nodiscard]] RpcHandler* find(std::string_view name) const noexcept;

private:
    RpcRegistry() = default;
    RpcRegistry(const RpcRegistry&) = delete;
    RpcRegistry& operator=(const RpcRegistry&) = delete;

    std::unordered_map<std::string, std::unique_ptr<RpcHandler>> handlers_;
};

}  // namespace lsw::agent::rpc
