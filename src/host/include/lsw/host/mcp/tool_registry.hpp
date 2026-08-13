#pragma once

#include "lsw/host/mcp/mcp_tool.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lsw::host::mcp {

// The set of tools this server exposes, and the ``tools/list`` document
// derived from it. Insertion order is preserved so the advertised list
// stays stable between runs.
class ToolRegistry {
public:
    ToolRegistry() = default;

    // Insert. The tool's own ``name()`` is the key; a duplicate name is
    // a programming error and replaces the earlier entry.
    void add(std::unique_ptr<IMcpTool> tool);

    // Look up by wire name; nullptr when unknown.
    [[nodiscard]] IMcpTool* find(std::string_view name) const;

    // The ``tools`` array for a ``tools/list`` reply, in insertion order.
    [[nodiscard]] nlohmann::json describe_all() const;

private:
    std::vector<std::unique_ptr<IMcpTool>> order_;
    std::unordered_map<std::string, IMcpTool*> by_name_;
};

}  // namespace lsw::host::mcp
