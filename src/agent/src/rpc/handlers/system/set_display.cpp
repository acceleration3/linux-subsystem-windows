#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <string>

namespace lsw::agent::rpc::handlers {

namespace {

// Create-or-open ``root\subkey`` and write one value; false when either
// step fails.
bool write_dword(HKEY root, const wchar_t* subkey, const wchar_t* name, std::uint32_t value) {
    const auto key = win::RegKey::create(root, subkey, KEY_SET_VALUE);
    return key && key->write_dword(name, value) == ERROR_SUCCESS;
}

}  // namespace

// Apply the host's display settings inside the guest. Only the DPI is
// left to apply from in here.
//
// The resolution is the host's business: it sets the display device's
// modes and Windows follows the adapter. The video memory is the host
// GPU's, and the adapter reports it.
//
// Windows reads the DPI when a session starts, so it needs a logon,
// which in practice means the next boot. That is not worth a reboot on
// its own, so nothing here forces one. The manager tells the user the
// setting is waiting.
struct SetDisplayHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        nlohmann::json reply = {{"dpi", false}};

        // 96 DPI is 100%; Windows stores the scaled figure, and
        // Win8DpiScaling is what tells it to honour a per-user one at
        // all.
        if (const int percent = params.value("scaling_percent", 0); percent > 0) {
            const auto dpi = static_cast<std::uint32_t>((percent * 96) / 100);
            const bool a =
                write_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"LogPixels", dpi);
            const bool b = write_dword(HKEY_CURRENT_USER, L"Control Panel\\Desktop",
                                       L"Win8DpiScaling", percent == 100 ? 0U : 1U);
            reply["dpi"] = a && b;
        }
        return reply;
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::string{"dpi="} + (reply.value("dpi", false) ? "1" : "0");
    }

    // Window handles, UI Automation and HKCU belong to the user's
    // session: served by the session helper there.
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetDisplayHandler, ::lsw::linkproto::cmd::kSetDisplay)
