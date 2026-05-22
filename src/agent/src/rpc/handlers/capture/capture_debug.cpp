#include "lsw/agent/capture/wgc.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>

namespace lsw::agent::rpc::handlers {

struct CaptureDebugHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        nlohmann::json r;
        r["wgc_available"] = capture::Wgc::available();
        r["pw"] = nlohmann::json::parse(win::Window::capture_debug_json(
            static_cast<std::uintptr_t>(hwnd)));
        r["wgc"] = nlohmann::json::parse(
            capture::Wgc::debug_json(static_cast<std::uintptr_t>(hwnd)));
        return r;
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::CaptureDebugHandler, "capture_debug")
