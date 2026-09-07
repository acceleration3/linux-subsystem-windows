#include "lsw/agent/apps/d3d_translation.hpp"
#include "lsw/agent/apps/launcher.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/log.hpp"

#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

struct LaunchHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto app_id = params.value("app_id", std::string{});
        // Per-app, because Windows only resolves a translation DLL from
        // the directory of the running image — there is no system-wide
        // way to say "use DXVK for this one". Off unless asked: staging
        // copies files into the application's own folder, which is not
        // something to do to every app on the chance it uses Direct3D.
        const bool translate_d3d = params.value("d3d", false);
        std::string d3d_err;
        if (translate_d3d && !apps::D3dTranslation::stage_for(app_id, &d3d_err)) {
            // Launch anyway. Without the DLLs the app uses the system
            // Direct3D, which on a guest with no GPU means WARP — slow,
            // but running, and far better than refusing to start.
            LSW_AGENT_WARN("launch: d3d translation unavailable: " + d3d_err);
        }

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
        if (translate_d3d) {
            r["d3d"] = d3d_err.empty();
            if (!d3d_err.empty()) {
                r["d3d_error"] = d3d_err;
            }
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
               + (err.empty() ? "" : " err=" + err);
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::LaunchHandler, ::lsw::linkproto::cmd::kLaunch)
