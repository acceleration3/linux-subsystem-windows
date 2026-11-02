#pragma once

namespace lsw::agent::core {
class AgentServices;
}

namespace lsw::agent::rpc {
struct RpcContext {
    core::AgentServices& services;
};

}
