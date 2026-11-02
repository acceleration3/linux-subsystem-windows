#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {
struct ListSessionsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& , RpcContext& ctx) override {
        return ctx.services.sessions().describe();
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("sessions", nlohmann::json::array()).size()) +
               " session(s)";
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListSessionsHandler,
                 ::lsw::linkproto::cmd::kListSessions)
