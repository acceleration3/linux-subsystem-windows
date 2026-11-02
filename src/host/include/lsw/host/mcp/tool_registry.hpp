#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lsw::host::mcp {
class ToolRegistry {
public:
    ToolRegistry() = default;

    void add(std::unique_ptr<IMcpTool> tool);

    [[nodiscard]] IMcpTool* find(std::string_view name) const;

    [[nodiscard]] nlohmann::json describe_all() const;

private:
    std::vector<std::unique_ptr<IMcpTool>> order_;
    std::unordered_map<std::string, IMcpTool*> by_name_;
};

}
