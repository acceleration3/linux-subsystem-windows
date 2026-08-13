#include "lsw/agent/apps/catalog.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/rpc/auto_register.hpp"

#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {

struct ListAppsHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/,
                          RpcContext& /*ctx*/) override {
        return nlohmann::json::parse(apps::Catalog::list_json());
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("apps", nlohmann::json::array()).size())
               + " apps, " + std::to_string(reply.dump().size()) + "B json";
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListAppsHandler, ::lsw::linkproto::cmd::kListApps)
