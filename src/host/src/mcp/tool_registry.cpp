#include "lsw/host/mcp/tool_registry.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

void ToolRegistry::add(std::unique_ptr<IMcpTool> tool) {
    if (tool == nullptr) {
        return;
    }
    auto& slot = by_name_[tool->name()];
    if (slot != nullptr) {
        // Replace in place, keeping the earlier entry's position.
        for (auto& existing : order_) {
            if (existing.get() == slot) {
                slot = tool.get();
                existing = std::move(tool);
                return;
            }
        }
    }
    slot = tool.get();
    order_.push_back(std::move(tool));
}

IMcpTool* ToolRegistry::find(std::string_view name) const {
    const auto it = by_name_.find(std::string{name});
    return it == by_name_.end() ? nullptr : it->second;
}

nlohmann::json ToolRegistry::describe_all() const {
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& t : order_) {
        tools.push_back({
            {"name", t->name()},
            {"description", t->description()},
            {"inputSchema", t->input_schema()},
        });
    }
    return tools;
}

}  // namespace lsw::host::mcp
