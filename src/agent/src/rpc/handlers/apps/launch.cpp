#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/apps/launcher.hpp"
#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/win/run_as.hpp"
#include "lsw/agent/win/window.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

// ``launch``: start an application.
//
//   params: app_id, args?, env?, session? (default: the console user's
//           session; 0 = SYSTEM in session 0), elevated? (default true)
//   reply:  {ok, pid, job?, session, elevated, error?}
struct LaunchHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto app_id = params.value("app_id", std::string{});

        // Extra arguments, and per-launch environment overrides as a
        // ``{"NAME": "value"}`` object. Overrides exist because some
        // applications are configured only through their environment
        // and the alternatives reach too far: a machine-wide variable
        // applies to every process and needs a logon to take effect.
        const auto args = params.value("args", std::string{});
        std::vector<apps::Launcher::EnvVar> env;
        if (const auto it = params.find("env"); it != params.end() && it->is_object()) {
            for (const auto& [name, value] : it->items()) {
                if (!name.empty() && value.is_string()) {
                    env.emplace_back(name, value.get<std::string>());
                }
            }
        }

        const win::RunAs as = ctx.services.sessions().run_as(
            params, linkproto::cmd::kLaunch, session::SessionManager::Default::UserSession);

        unsigned long pid = 0;
        std::string err;
        const auto mode = params.value("reuse_existing", false) ? apps::Launcher::Mode::Open
                                                                : apps::Launcher::Mode::NewInstance;
        std::vector<unsigned long> visible_pids;
        if (mode == apps::Launcher::Mode::Open && as.session() != 0) {
            DWORD own_session = 0;
            ::ProcessIdToSessionId(::GetCurrentProcessId(), &own_session);
            if (own_session == as.session()) {
                for (const auto& window : win::Window::enumerate()) {
                    if (!window.title.empty())
                        visible_pids.push_back(window.pid);
                }
            } else {
                const auto listed = ctx.services.sessions().route(
                    linkproto::cmd::kListWindows, {{"session", as.session()}});
                for (const auto& window : listed.value("windows", nlohmann::json::array())) {
                    if (!window.value("title", std::string{}).empty())
                        visible_pids.push_back(window.value("pid", 0UL));
                }
            }
        }
        const bool ok = apps::Launcher::launch(app_id, args, env, as, &pid, &err, mode,
                                              visible_pids);
        nlohmann::json r = {{"ok", ok},
                            {"pid", static_cast<std::uint64_t>(pid)},
                            {"session", as.session()},
                            {"elevated", as.elevated()}};
        // What this launch is, for anything that has to recognise its
        // windows later. The caller is spared the naming convention.
        if (ok && pid != 0) {
            if (const auto root = apps::LaunchGroup::root_of(pid); root != 0) {
                r["job"] = apps::LaunchGroup::job_name(root);
            }
        }
        if (!ok && !err.empty()) {
            r["error"] = err;
        }
        return r;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        const auto err = reply.value("error", std::string{});
        return std::string{"ok="} + (reply.value("ok", false) ? "true" : "false") +
               " pid=" + std::to_string(reply.value("pid", std::uint64_t{0})) +
               " job=" + reply.value("job", std::string{}) + (err.empty() ? "" : " err=" + err);
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::LaunchHandler, ::lsw::linkproto::cmd::kLaunch)
