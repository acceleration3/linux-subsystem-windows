#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// ``uia_get_text``: return the text content of ``hwnd`` via UIA's
// TextPattern. ``max_chars`` caps the response so reading a huge
// Notepad doesn't pin the agent on a multi-MB BSTR; default 32k
// is plenty for any UI surface we'd reasonably scrape.
struct UiaGetTextHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("uia_get_text: missing hwnd");
        }
        const int max_chars = params.value("max_chars", 32 * 1024);
        return {{"text", ctx.services.uia().get_text(
                            static_cast<std::uintptr_t>(hwnd), max_chars)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaGetTextHandler, ::lsw::linkproto::cmd::kUiaGetText)
