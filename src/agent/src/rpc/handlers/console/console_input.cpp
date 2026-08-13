#include "lsw/agent/console/console.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct ConsoleInputHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto sid = params.value("session_id", std::string{});
        const auto data = params.value("data", std::string{});
        if (sid.empty()) {
            throw std::invalid_argument("console_input: missing session_id");
        }
        const bool ok = ctx.services.console().write(sid, data);
        return {{"ok", ok}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ConsoleInputHandler, ::lsw::linkproto::cmd::kConsoleInput)
