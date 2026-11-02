#include "lsw/agent/apps/catalog.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/linkproto/commands.hpp"

#include <string>

namespace lsw::agent::rpc::handlers {
struct ListAppsHandler : RpcHandler {
    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    nlohmann::json handle(const nlohmann::json& , RpcContext& ) override {
        const win::Apartment apartment(win::Apartment::Model::SingleThreaded);
        return apps::Catalog::list();
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("apps", nlohmann::json::array()).size()) + " apps, " +
               std::to_string(reply.dump().size()) + "B json";
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListAppsHandler, ::lsw::linkproto::cmd::kListApps)
