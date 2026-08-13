#include "lsw/agent/capture/setup.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

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
        // The DLL path is Vulkan-only: enabling Vulkan capture hardlinks
        // the DXVK / VKD3D proxy DLLs next to the exe. Any other method
        // (WGC) is a no-op here — we deliberately do NOT run the old
        // "disable" path, which DeleteFile()d a fixed d3d*/dxgi list by
        // name in the app's folder and would destroy DLLs the app
        // itself ships. WGC needs no DLLs and must never touch them.
        if (method != "vulkan") {
            return {{"ok", true}};
        }
        const bool ok = capture::Setup::enable_vulkan_for(path);
        return {{"ok", ok}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetCaptureMethodHandler, ::lsw::linkproto::cmd::kSetCaptureMethod)
