#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

// ``uia_get_text``: return the text content of ``hwnd`` via UIA's
// TextPattern. ``max_chars`` caps the response so reading a huge
// Notepad doesn't pin the agent on a multi-MB BSTR; default 32k
// is plenty for any UI surface we'd reasonably scrape.
struct UiaGetTextHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kUiaGetText};
        const auto hwnd = p.require_hwnd();
        const int max_chars = p.get("max_chars", 32 * 1024);
        return {{"text", ctx.services.uia().get_text(hwnd, max_chars)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaGetTextHandler, ::lsw::linkproto::cmd::kUiaGetText)
