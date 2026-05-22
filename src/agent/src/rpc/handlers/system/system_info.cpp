#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"
#include "lsw/agent/system/info.hpp"

namespace lsw::agent::rpc::handlers {

struct SystemInfoHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/,
                          RpcContext& /*ctx*/) override {
        return nlohmann::json::parse(system::Info::collect_json());
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SystemInfoHandler, "system_info")
