#include "lsw/agent/capture/setup.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct SetCaptureMethodHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto path = params.value("path", std::string{});
        const auto method = params.value("method", std::string{});
        if (path.empty() || method.empty()) {
            throw std::invalid_argument(
                "set_capture_method: missing path or method");
        }
        const bool ok = (method == "vulkan")
                            ? capture::Setup::enable_vulkan_for(path)
                            : capture::Setup::disable_vulkan_for(path);
        return {{"ok", ok}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetCaptureMethodHandler,
                 "set_capture_method")
