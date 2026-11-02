#pragma once

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

class ToolRegistry;

class JsonRpcRouter {
public:
    JsonRpcRouter(ToolRegistry& tools, link::AgentRpcClient& agent) noexcept
        : tools_(tools), agent_(agent) {}

    [[nodiscard]] nlohmann::json dispatch(const nlohmann::json& request);

private:
    [[nodiscard]] static nlohmann::json on_initialize();
    [[nodiscard]] nlohmann::json on_tools_list() const;
    [[nodiscard]] nlohmann::json on_tools_call(const nlohmann::json& params);

    ToolRegistry& tools_;
    link::AgentRpcClient& agent_;
};

}
