#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/gdi.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

struct GetWindowIconHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("get_window_icon: missing hwnd");
        }
        return {{"icon_png_base64",
                 gdi::extract_window_icon_png_base64(
                     reinterpret_cast<void*>(static_cast<std::uintptr_t>(hwnd)))}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::GetWindowIconHandler, "get_window_icon")
