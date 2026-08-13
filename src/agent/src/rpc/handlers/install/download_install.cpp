#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct DownloadInstallHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto url = params.value("url", std::string{});
        if (url.empty()) {
            throw std::invalid_argument("download_install: missing url");
        }
        return {{"ok", true},
                {"job_id", ctx.services.installer().start_download_install(
                                params.value("name", std::string{}), url,
                                params.value("args", std::string{}))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::DownloadInstallHandler, ::lsw::linkproto::cmd::kDownloadInstall)
