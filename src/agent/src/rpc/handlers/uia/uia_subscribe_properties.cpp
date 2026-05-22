#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// Watch a single window's WindowVisualState / Name / IsModal
// properties. Fires distinct topics (``uia_state_changed`` /
// ``uia_title_changed`` / ``uia_modal_changed``) so the host can
// route each via ``on_event`` without re-parsing the payload.
struct UiaSubscribePropertiesHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument(
                "uia_subscribe_properties: missing hwnd");
        }
        const auto id = ctx.services.uia().subscribe_window_properties(
            static_cast<std::uintptr_t>(hwnd));
        return {{"ok", id != 0}, {"sub_id", id}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSubscribePropertiesHandler,
                 "uia_subscribe_properties")
