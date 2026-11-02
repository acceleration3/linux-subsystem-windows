#include "lsw/agent/rpc/registry.hpp"

#include "lsw/agent/log.hpp"

#include <cstdlib>

namespace lsw::agent::rpc {

RpcRegistry& RpcRegistry::instance() {
    static RpcRegistry r;
    return r;
}

void RpcRegistry::register_handler(std::string name, std::unique_ptr<RpcHandler> h) {
    if (handlers_.find(name) != handlers_.end()) {
        LSW_AGENT_ERROR("rpc: duplicate handler '{}'", name);
        std::abort();
    }
    handlers_.emplace(std::move(name), std::move(h));
}

RpcHandler* RpcRegistry::find(std::string_view name) const noexcept {
    const auto it = handlers_.find(std::string{name});
    return it == handlers_.end() ? nullptr : it->second.get();
}

}
