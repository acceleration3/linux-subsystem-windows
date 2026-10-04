#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

struct DownloadInstallHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kDownloadInstall};
        const auto url = p.require_string("url");
        return {{"ok", true},
                {"job_id", ctx.services.installer().start_download_install(
                               p.get("name", std::string{}), url, p.get("args", std::string{}))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::DownloadInstallHandler,
                 ::lsw::linkproto::cmd::kDownloadInstall)
