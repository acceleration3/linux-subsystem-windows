#include "lsw/agent/console/console.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

struct ConsoleCloseHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto sid = params.value("session_id", std::string{});
        if (!sid.empty()) {
            ctx.services.console().close(sid);
        }
        return {{"ok", true}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ConsoleCloseHandler, ::lsw::linkproto::cmd::kConsoleClose)
