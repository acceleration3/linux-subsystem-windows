#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

// Processes run as SYSTEM in session 0 by default; ``session: N`` runs
// them as session N's user (``elevated``, default true, picks the full
// token). Reply: {ok, job_id, session}.
struct DownloadInstallHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kDownloadInstall};
        // SYSTEM in session 0 unless the request names a user session.
        const win::RunAs as = ctx.services.sessions().run_as(
            params, linkproto::cmd::kDownloadInstall, session::SessionManager::Default::System);
        const auto url = p.require_string("url");
        return {{"ok", true},
                {"session", as.session()},
                {"job_id", ctx.services.installer().start_download_install(
                               p.get("name", std::string{}), url, p.get("args", std::string{}),
                               as)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::DownloadInstallHandler,
                 ::lsw::linkproto::cmd::kDownloadInstall)
