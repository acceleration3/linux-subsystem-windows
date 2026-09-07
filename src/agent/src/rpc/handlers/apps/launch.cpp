#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/apps/launcher.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

struct LaunchHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto app_id = params.value("app_id", std::string{});

        // Extra arguments, and per-launch environment overrides as a
        // ``{"NAME": "value"}`` object. Overrides exist because some
        // applications are configured only through their environment
        // and the alternatives reach too far: a machine-wide variable
        // applies to every process and needs a logon to take effect.
        const auto args = params.value("args", std::string{});
        std::vector<apps::Launcher::EnvVar> env;
        if (const auto it = params.find("env");
            it != params.end() && it->is_object()) {
            for (const auto& [name, value] : it->items()) {
                if (!name.empty() && value.is_string()) {
                    env.emplace_back(name, value.get<std::string>());
                }
            }
        }

        unsigned long pid = 0;
        std::string err;
        const bool ok = apps::Launcher::launch(app_id, args, env, &pid, &err);
        nlohmann::json r = {{"ok", ok}, {"pid", static_cast<std::uint64_t>(pid)}};
        // What this launch is, for anything that has to recognise its
        // windows later. The caller is spared the naming convention.
        if (ok && pid != 0) {
            r["job"] = apps::LaunchGroup::job_name(pid);
        }
        if (!ok && !err.empty()) {
            r["error"] = err;
        }
        return r;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        const auto err = reply.value("error", std::string{});
        return std::string{"ok="} + (reply.value("ok", false) ? "true" : "false")
               + " pid=" + std::to_string(reply.value("pid", std::uint64_t{0}))
               + " job=" + reply.value("job", std::string{})
               + (err.empty() ? "" : " err=" + err);
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::LaunchHandler, ::lsw::linkproto::cmd::kLaunch)
