#include "lsw/agent/apps/launcher.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

struct LaunchHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto app_id = params.value("app_id", std::string{});
        unsigned long pid = 0;
        std::string err;
        const bool ok = apps::Launcher::launch(app_id, &pid, &err);
        nlohmann::json r = {{"ok", ok}, {"pid", static_cast<std::uint64_t>(pid)}};
        if (!ok && !err.empty()) {
            r["error"] = err;
        }
        return r;
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::LaunchHandler, "launch")
