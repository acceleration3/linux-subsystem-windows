#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handlers/uia_unsubscribe.hpp"
#include "lsw/linkproto/commands.hpp"

// The pre-generic spelling of ``uia_unsubscribe``; same handler.
LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaUnsubscribeHandler,
                 ::lsw::linkproto::cmd::kUiaUnsubscribeLifecycle)
