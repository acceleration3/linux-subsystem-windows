#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/system/info.hpp"
#include "lsw/linkproto/commands.hpp"

namespace lsw::agent::rpc::handlers {

struct SystemInfoHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& , RpcContext& ) override {
        return system::Info::collect();
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SystemInfoHandler, ::lsw::linkproto::cmd::kSystemInfo)
