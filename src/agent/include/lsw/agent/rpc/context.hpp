#pragma once

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::rpc {

class Session;

// The bundle of references every handler receives. Kept small: a ref
// to the shared service container and a ref to the per-connection
// session. Handlers should reach for things via these rather than
// global singletons so they remain testable.
struct RpcContext {
    core::AgentServices& services;
    Session& session;
};

}  // namespace lsw::agent::rpc
