#include "lsw/agent/core/services.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// ``uia_tree_dump``: recursive walk of the UIA tree under ``hwnd``,
// returning a JSON object with each element's control_type, name,
// automation_id, bounding rect (screen px), and children. Used for
// debugging "what's the AutomationId of this thing" without bringing
// up Accessibility Insights inside the VM. ``max_depth`` caps
// recursion to keep replies bounded.
struct UiaTreeDumpHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& ctx) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("uia_tree_dump: missing hwnd");
        }
        const int max_depth = params.value("max_depth", 4);
        return {{"tree", ctx.services.uia().dump_tree(
                            static_cast<std::uintptr_t>(hwnd), max_depth)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::UiaTreeDumpHandler, "uia_tree_dump")
