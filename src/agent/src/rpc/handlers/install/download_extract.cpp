#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

struct DownloadExtractHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kDownloadExtract};
        const auto url = p.require_string("url");
        return {{"ok", true},
                {"job_id",
                 ctx.services.installer().start_download_extract(p.get("name", std::string{}), url,
                                                                 p.get("extract_to", std::string{}),
                                                                 p.get("script", std::string{}))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::DownloadExtractHandler,
                 ::lsw::linkproto::cmd::kDownloadExtract)
