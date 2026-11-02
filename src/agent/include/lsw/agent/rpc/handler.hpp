#pragma once

#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/context.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::agent::rpc {
class RpcHandler {
public:
    enum class Placement {
        Service,
        UserSession,
    };

    virtual ~RpcHandler() = default;
    [[nodiscard]] virtual nlohmann::json handle(const nlohmann::json& params,
                                                 RpcContext& ctx) = 0;

    [[nodiscard]] virtual log::Level request_level() const { return log::Level::Debug; }

    [[nodiscard]] virtual std::string summarize(const nlohmann::json& reply) const {
        return std::to_string(reply.dump().size()) + "B json";
    }

    [[nodiscard]] virtual Placement placement() const { return Placement::Service; }

    virtual void complete_in_service(nlohmann::json& reply) const { (void)reply; }
};

}
