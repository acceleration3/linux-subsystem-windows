#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

// ``list_sessions``: the Windows sessions with a user logged on, for a
// manager that wants to pick one for ``launch``, ``console_open`` and the
// other session-aware commands.
//
//   {"sessions": [{"id": 1, "user": "alice", "state": "active",
//                  "console": true, "helper": true}],
//    "default": 1}
//
// ``state`` is active | connected | disconnected. ``console``: the session
// on the VM's display. ``helper``: its session helper is connected, so
// window and UI Automation commands can reach it. ``default`` is where a
// request without ``session`` goes (null when nobody is logged on).
// Session 0 (SYSTEM, no desktop) is never listed; ``session: 0`` still
// selects it where running as SYSTEM makes sense.
struct ListSessionsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/, RpcContext& ctx) override {
        return ctx.services.sessions().describe();
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("sessions", nlohmann::json::array()).size()) +
               " session(s)";
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListSessionsHandler,
                 ::lsw::linkproto::cmd::kListSessions)
