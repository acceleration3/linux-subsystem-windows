#include "lsw/agent/rpc/registry.hpp"

#include "lsw/agent/log.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace lsw::agent::rpc {

RpcRegistry& RpcRegistry::instance() {
    static RpcRegistry r;
    return r;
}

void RpcRegistry::register_handler(std::string name,
                                    std::unique_ptr<RpcHandler> h) {
    // Duplicate registration is always a coding error: two TUs both
    // claimed the same command. Log + abort — a silent overwrite would
    // route the command to whichever TU's static init won the race.
    if (handlers_.find(name) != handlers_.end()) {
        LSW_AGENT_ERROR(std::string{"rpc: duplicate handler '"} + name + "'");
        std::abort();
    }
    handlers_.emplace(std::move(name), std::move(h));
}

RpcHandler* RpcRegistry::find(std::string_view name) const noexcept {
    // unordered_map::find with string_view requires C++20 heterogeneous
    // lookup; we're on C++17, so construct a string. Cost is in the
    // dispatch hot path but the alternative is a transparent-hash
    // unordered_map which is C++20.
    const auto it = handlers_.find(std::string{name});
    return it == handlers_.end() ? nullptr : it->second.get();
}

std::vector<std::string> RpcRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(handlers_.size());
    for (const auto& kv : handlers_) {
        out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace lsw::agent::rpc
