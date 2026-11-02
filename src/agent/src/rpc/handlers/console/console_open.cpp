#include "lsw/agent/console/console.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/win/run_as.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {
struct ConsoleOpenHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const win::RunAs as = ctx.services.sessions().run_as(
            params, linkproto::cmd::kConsoleOpen, session::SessionManager::Default::UserSession);
        const auto sid = ctx.services.console().open(as);
        if (sid.empty()) {
            throw std::runtime_error("console_open: failed to spawn cmd.exe as " + as.describe());
        }
        return {{"ok", true},
                {"session_id", sid},
                {"session", as.session()},
                {"user", as.account()},
                {"elevated", as.elevated()}};
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ConsoleOpenHandler, ::lsw::linkproto::cmd::kConsoleOpen)
