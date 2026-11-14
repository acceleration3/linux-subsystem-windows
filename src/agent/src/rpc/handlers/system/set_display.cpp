#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/system/display_request.hpp"
#include "lsw/agent/win/display_resolution.hpp"
#include "lsw/agent/win/display_scale.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <string>

namespace lsw::agent::rpc::handlers {

namespace {
bool clear_custom_scaling() {
    const auto key = win::RegKey::open(HKEY_CURRENT_USER, L"Control Panel\\Desktop",
                                       KEY_SET_VALUE);
    if (!key) {
        return false;
    }
    const long removed = key->delete_value(L"LogPixels");
    return (removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND) &&
           key->write_dword(L"Win8DpiScaling", 0) == ERROR_SUCCESS;
}

}

struct SetDisplayHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        nlohmann::json reply = {{"dpi", false}};
        const system::DisplayRequest request(params);
        if (const auto& wanted = request.resolution()) {
            LSW_AGENT_INFO("set_display: resolution {}x{}", wanted->width, wanted->height);
            reply["resolution"] = win::DisplayResolution::apply(*wanted);
        }
        if (const int percent = request.scaling_percent(); percent > 0) {
            LSW_AGENT_INFO("set_display: scaling {} %", percent);
            const bool cleared = clear_custom_scaling();
            const win::DisplayScale::Outcome outcome = win::DisplayScale::apply(percent);
            if (!cleared) {
                LSW_AGENT_WARN("set_display: the custom scaling could not be cleared");
            }
            reply["dpi"] = cleared && outcome.displays > 0;
            reply["scale_percent"] = outcome.percent;
            reply["displays"] = outcome.displays;
        }
        system::DisplayRequest::answer(reply, win::DisplayResolution::current());
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::string{"dpi="} + (reply.value("dpi", false) ? "1" : "0") + " scale=" +
               std::to_string(reply.value("scale_percent", 0)) + "% on " +
               std::to_string(reply.value("displays", 0)) + " display(s), now " +
               std::to_string(reply.value("width", 0)) + "x" + std::to_string(reply.value("height", 0));
    }

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetDisplayHandler, ::lsw::linkproto::cmd::kSetDisplay)
