#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

// ``uia_invoke``: find an element under ``hwnd`` whose AutomationId
// (preferred) or Name matches ``selector`` and fire its
// InvokePattern — the UIA equivalent of clicking it. Returns
// ``{"ok": bool}``. Used by automation scripts to drive dialogs by
// element identity instead of pixel coordinates.
struct UiaInvokeHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        const auto selector = params.value("selector", std::string{});
        if (hwnd == 0 || selector.empty()) {
            throw std::invalid_argument("uia_invoke: missing hwnd or selector");
        }
        const bool ok = ctx.services.uia().invoke_by_selector(
            static_cast<std::uintptr_t>(hwnd), selector);
        return {{"ok", ok}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaInvokeHandler, ::lsw::linkproto::cmd::kUiaInvoke)
