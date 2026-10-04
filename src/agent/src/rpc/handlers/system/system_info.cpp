#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/system/info.hpp"
#include "lsw/linkproto/commands.hpp"

namespace lsw::agent::rpc::handlers {

struct SystemInfoHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/, RpcContext& /*ctx*/) override {
        return system::Info::collect();
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SystemInfoHandler, ::lsw::linkproto::cmd::kSystemInfo)
