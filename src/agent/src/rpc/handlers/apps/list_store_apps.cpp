#include "lsw/agent/apps/store_apps.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/linkproto/commands.hpp"

#include <algorithm>
#include <string>

namespace lsw::agent::rpc::handlers {
struct ListStoreAppsHandler : RpcHandler {
    static constexpr int kDefaultIconSize = 48;
    static constexpr int kSmallestIcon = 16;
    static constexpr int kLargestIcon = 256;

    [[nodiscard]] Placement placement() const override { return Placement::UserSession; }

    nlohmann::json handle(const nlohmann::json& params, RpcContext& ) override {
        const int size =
            std::clamp(params.value("icon_size", kDefaultIconSize), kSmallestIcon, kLargestIcon);
        const auto only = util::TextEncoding::to_wide(params.value("aumid", std::string{}));
        const win::Apartment apartment(win::Apartment::Model::SingleThreaded);
        return apps::StoreApps::list(size, only);
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        return std::to_string(reply.value("apps", nlohmann::json::array()).size()) + " store apps, " +
               std::to_string(reply.dump().size()) + "B json";
    }
};

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::ListStoreAppsHandler,
                 ::lsw::linkproto::cmd::kListStoreApps)
