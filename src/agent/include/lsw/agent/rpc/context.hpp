#pragma once

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::rpc {

// What every handler receives. Kept small: a ref to the shared service
// container. Handlers should reach for things via this rather than
// global singletons so they remain testable.
struct RpcContext {
    core::AgentServices& services;
};

}  // namespace lsw::agent::rpc
