#include "lsw/agent/apps/store_apps.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

// ``launch_store_app``: activate a packaged application by its ID.
//
//   params: aumid, args?, session?
//   reply:  {ok, pid}
//
// The process is started by Windows' activation broker, not by the
// agent, so it belongs to no job of the agent's: the mirror follows it by
// its pid.
struct LaunchStoreAppHandler : RpcHandler {
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const auto aumid = Params{params, linkproto::cmd::kLaunchStoreApp}.require_string("aumid");
        const auto args = params.value("args", std::string{});
        const win::Apartment apartment(win::Apartment::Model::SingleThreaded);
        const unsigned long pid = apps::StoreApps::activate(util::TextEncoding::to_wide(aumid),
                                                            util::TextEncoding::to_wide(args));
        return {{"ok", true}, {"pid", static_cast<std::uint64_t>(pid)}};
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return "pid=" + std::to_string(reply.value("pid", std::uint64_t{0}));
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::LaunchStoreAppHandler,
                 ::lsw::linkproto::cmd::kLaunchStoreApp)
