#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/system/log_clearer.hpp"
#include "lsw/linkproto/commands.hpp"

namespace lsw::agent::rpc::handlers {

struct ClearLogsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& , RpcContext& ) override {
        return system::LogClearer{}.run();
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Info; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ClearLogsHandler, ::lsw::linkproto::cmd::kClearLogs)
