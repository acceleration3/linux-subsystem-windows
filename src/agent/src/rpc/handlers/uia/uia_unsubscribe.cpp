#include "lsw/agent/rpc/handlers/uia_unsubscribe.hpp"

#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/linkproto/commands.hpp"

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaUnsubscribeHandler,
                 ::lsw::linkproto::cmd::kUiaUnsubscribe)
