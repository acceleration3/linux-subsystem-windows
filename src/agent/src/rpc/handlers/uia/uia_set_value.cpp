#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/params.hpp"
#include "lsw/agent/uia/uia.hpp"
#include "lsw/linkproto/commands.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

struct UiaSetValueHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ctx) override {
        const Params p{params, linkproto::cmd::kUiaSetValue};
        const auto hwnd = p.hwnd();
        const auto selector = p.get("selector", std::string{});
        const auto value = p.get("value", std::string{});
        if (hwnd == 0 || selector.empty()) {
            p.fail_missing("hwnd or selector");
        }
        return {{"ok", ctx.services.uia().set_value(hwnd, selector, value)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSetValueHandler, ::lsw::linkproto::cmd::kUiaSetValue)
