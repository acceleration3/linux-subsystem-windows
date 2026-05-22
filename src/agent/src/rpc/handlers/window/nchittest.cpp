#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/window.hpp"

#include <cstdint>
#include <stdexcept>

namespace lsw::agent::rpc::handlers {

// Single-purpose hit-test the host fires AFTER its cached UIA
// button-rect check misses on a left-press. Returns the raw
// ``WM_NCHITTEST`` code so the host can distinguish HTCAPTION
// (start system move) from HTCLIENT (forward to guest as a normal
// click). Wraps ``win::Window::hit_test_nc`` 1:1 — no diagnostic
// fields anymore.
struct NcHitTestHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) {
            throw std::invalid_argument("nchittest: missing hwnd");
        }
        const int x = params.value("x", 0);
        const int y = params.value("y", 0);
        return {{"ht", win::Window::hit_test_nc(
                          static_cast<std::uintptr_t>(hwnd), x, y)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::NcHitTestHandler, "nchittest")
