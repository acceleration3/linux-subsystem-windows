#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct DownloadExtractHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto url = params.value("url", std::string{});
        if (url.empty()) {
            throw std::invalid_argument("download_extract: missing url");
        }
        return {{"ok", true},
                {"job_id", ctx.services.installer().start_download_extract(
                                params.value("name", std::string{}), url,
                                params.value("extract_to", std::string{}),
                                params.value("script", std::string{}))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::DownloadExtractHandler, ::lsw::linkproto::cmd::kDownloadExtract)
