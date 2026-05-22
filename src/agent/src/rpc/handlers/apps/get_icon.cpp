#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/win/gdi.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

struct GetIconHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params,
                          RpcContext& /*ctx*/) override {
        const auto path = params.value("path", std::string{});
        if (path.empty()) {
            throw std::invalid_argument("get_icon: missing path");
        }
        const int wlen =
            ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wpath(static_cast<size_t>(wlen > 0 ? wlen - 1 : 0), L'\0');
        if (wlen > 0) {
            ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(),
                                  wlen);
        }
        return {{"icon_png_base64", gdi::extract_icon_png_base64(wpath)}};
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::GetIconHandler, "get_icon")
