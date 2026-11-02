#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/system/log_packer.hpp"
#include "lsw/linkproto/commands.hpp"

namespace lsw::agent::rpc::handlers {

struct PackLogsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& , RpcContext& ) override {
        return system::LogPacker::pack();
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::PackLogsHandler, ::lsw::linkproto::cmd::kPackLogs)
