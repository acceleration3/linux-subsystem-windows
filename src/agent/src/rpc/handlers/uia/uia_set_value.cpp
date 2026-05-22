#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct UiaSetValueHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        const auto selector = params.value("selector", std::string{});
        const auto value = params.value("value", std::string{});
        if (hwnd == 0 || selector.empty()) {
            throw std::invalid_argument(
                "uia_set_value: missing hwnd or selector");
        }
        const bool ok = ctx.services.uia().set_value(
            static_cast<std::uintptr_t>(hwnd), selector, value);
        return {{"ok", ok}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaSetValueHandler, "uia_set_value")
