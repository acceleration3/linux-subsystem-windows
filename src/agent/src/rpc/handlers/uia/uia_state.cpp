#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct UiaStateHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("uia_state: missing hwnd");
        }
        const auto s = ctx.services.uia().get_window_state(
            static_cast<std::uintptr_t>(hwnd));
        return {{"present", s.present},
                {"visual_state", s.visual_state},
                {"is_modal", s.is_modal},
                {"is_topmost", s.is_topmost}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaStateHandler, "uia_state")
