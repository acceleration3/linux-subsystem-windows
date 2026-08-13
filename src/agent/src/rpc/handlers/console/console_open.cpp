#include "lsw/agent/console/console.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct ConsoleOpenHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/,
                          RpcContext& ctx) override {
        const auto sid = ctx.services.console().open();
        if (sid.empty()) {
            throw std::runtime_error("console_open: failed to spawn cmd.exe");
        }
        return {{"ok", true}, {"session_id", sid}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ConsoleOpenHandler, ::lsw::linkproto::cmd::kConsoleOpen)
