#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/win/display_scale.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <string>

namespace lsw::agent::rpc::handlers {

namespace {

// Windows' "custom scaling": one DPI for every display, read only when a
// session starts, and while it is in force the per-display scale is not
// used. Off, so the scale this sets is the one Windows draws at.
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

}  // namespace

// Apply the host's display settings inside the guest. Only the scale is
// left to apply from in here.
//
// The resolution is the host's business: it sets the display device's
// modes and Windows follows the adapter. The video memory is the host
// GPU's, and the adapter reports it.
//
// The scale is set on every display at once, in Windows' steps
// (win::DisplayScale), so a percentage between two of them becomes the
// nearer.
//
//   params: scaling_percent
//   reply:  {dpi, scale_percent (the step it became), displays}
struct SetDisplayHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        nlohmann::json reply = {{"dpi", false}};
        const int percent = params.value("scaling_percent", 0);
        if (percent <= 0) {
            return reply;
        }
        const bool cleared = clear_custom_scaling();
        const win::DisplayScale::Outcome outcome = win::DisplayScale::apply(percent);
        reply["dpi"] = cleared && outcome.displays > 0;
        reply["scale_percent"] = outcome.percent;
        reply["displays"] = outcome.displays;
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::string{"dpi="} + (reply.value("dpi", false) ? "1" : "0") + " scale=" +
               std::to_string(reply.value("scale_percent", 0)) + "% on " +
               std::to_string(reply.value("displays", 0)) + " display(s)";
    }

    // The displays and HKCU belong to the user's session: served by the
    // session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetDisplayHandler, ::lsw::linkproto::cmd::kSetDisplay)
