#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct UiaStateHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto hwnd = Params{params, linkproto::cmd::kUiaState}.require_hwnd();
        const auto s = ctx.services.uia().get_window_state(hwnd);
        return {{"present", s.present},
                {"visual_state", s.visual_state},
                {"is_modal", s.is_modal},
                {"is_topmost", s.is_topmost}};
    }

    // Window handles, UI Automation and HKCU belong to the user's
    // session: served by the session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaStateHandler, ::lsw::linkproto::cmd::kUiaState)
