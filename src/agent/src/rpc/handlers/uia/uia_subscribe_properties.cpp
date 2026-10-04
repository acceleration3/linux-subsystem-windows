#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

// Watch a single window's WindowVisualState / Name / IsModal
// properties. Fires distinct topics (``uia_state_changed`` /
// ``uia_title_changed`` / ``uia_modal_changed``) so the host can
// route each via ``on_event`` without re-parsing the payload.
struct UiaSubscribePropertiesHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const auto hwnd = Params{params, linkproto::cmd::kUiaSubscribeProperties}.require_hwnd();
        const auto id = ctx.services.uia().subscribe_window_properties(hwnd);
        return {{"ok", id != 0}, {"sub_id", id}};
    }

    // Window handles, UI Automation and HKCU belong to the user's
    // session: served by the session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSubscribePropertiesHandler,
                 ::lsw::linkproto::cmd::kUiaSubscribeProperties)
