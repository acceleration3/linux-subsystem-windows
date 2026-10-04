#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

// ``uia_invoke``: find an element under ``hwnd`` whose AutomationId
// (preferred) or Name matches ``selector`` and fire its
// InvokePattern — the UIA equivalent of clicking it. Returns
// ``{"ok": bool}``. Used by automation scripts to drive dialogs by
// element identity instead of pixel coordinates.
struct UiaInvokeHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kUiaInvoke};
        const auto hwnd = p.hwnd();
        const auto selector = p.get("selector", std::string{});
        if (hwnd == 0 || selector.empty()) {
            p.fail_missing("hwnd or selector");
        }
        return {{"ok", ctx.services.uia().invoke_by_selector(hwnd, selector)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaInvokeHandler, ::lsw::linkproto::cmd::kUiaInvoke)
