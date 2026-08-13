#include "lsw/agent/core/services.hpp"
#include "lsw/agent/install/installer.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct RunScriptHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto script = params.value("script", std::string{});
        if (script.empty()) {
            throw std::invalid_argument("run_script: missing script");
        }
        return {{"ok", true},
                {"job_id", ctx.services.installer().start_run_script(
                                params.value("name", std::string{}), script)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::RunScriptHandler, ::lsw::linkproto::cmd::kRunScript)
