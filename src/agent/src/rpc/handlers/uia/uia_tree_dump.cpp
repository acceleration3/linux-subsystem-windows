#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

// ``uia_tree_dump``: recursive walk of the UIA tree under ``hwnd``,
// returning a JSON object with each element's control_type, name,
// automation_id, bounding rect (screen px), and children. Used for
// debugging "what's the AutomationId of this thing" without bringing
// up Accessibility Insights inside the VM. ``max_depth`` caps
// recursion to keep replies bounded.
struct UiaTreeDumpHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kUiaTreeDump};
        const auto hwnd = p.require_hwnd();
        const int max_depth = p.get("max_depth", 4);
        return {{"tree", ctx.services.uia().dump_tree(hwnd, max_depth)}};
    }

    // Window handles, UI Automation and HKCU belong to the user's
    // session: served by the session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaTreeDumpHandler, ::lsw::linkproto::cmd::kUiaTreeDump)
