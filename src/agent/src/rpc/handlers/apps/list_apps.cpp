#include "lsw/agent/apps/catalog.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/registry.hpp"

namespace lsw::agent::rpc::handlers {

struct ListAppsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/,
                          RpcContext& /*ctx*/) override {
        return nlohmann::json::parse(apps::Catalog::list_json());
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListAppsHandler, "list_apps")
