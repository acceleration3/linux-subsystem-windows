#pragma once

#include "lsw/agent/rpc/handler.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lsw::agent::rpc {
class RpcRegistry {
public:
    static RpcRegistry& instance();

    void register_handler(std::string name, std::unique_ptr<RpcHandler> h);

    [[nodiscard]] RpcHandler* find(std::string_view name) const noexcept;

private:
    RpcRegistry() = default;
    RpcRegistry(const RpcRegistry&) = delete;
    RpcRegistry& operator=(const RpcRegistry&) = delete;

    std::unordered_map<std::string, std::unique_ptr<RpcHandler>> handlers_;
};

}
