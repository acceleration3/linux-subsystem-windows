#include "lsw/agent/console/console.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/win/run_as.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// ``console_open``: a cmd.exe whose I/O rides the link.
//
//   params: session? (default: the console user's session, as that user;
//           0 = SYSTEM in session 0), elevated? (default true)
//   reply:  {ok, session_id: "c1", session, user, elevated}
//
// ``session_id`` is the console's own id (what console_input and
// console_close take); ``session`` is the Windows session it runs in.
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

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ConsoleOpenHandler, ::lsw::linkproto::cmd::kConsoleOpen)
