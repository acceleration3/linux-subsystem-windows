#include "lsw/host/mcp/tool_args.hpp"

#include "lsw/host/mcp/tool_result.hpp"

namespace lsw::host::mcp {

const nlohmann::json* ToolArgs::find(std::string_view key) const {
    if (!args_.is_object()) {
        return nullptr;
    }
    const auto it = args_.find(key);
    return it == args_.end() ? nullptr : &*it;
}

std::string ToolArgs::string(std::string_view key) const {
    const auto* value = find(key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string{};
}

std::int64_t ToolArgs::integer(std::string_view key, std::int64_t fallback) const {
    const auto* value = find(key);
    return value != nullptr && value->is_number_integer() ? value->get<std::int64_t>()
                                                          : fallback;
}

nlohmann::json ToolArgs::object(std::string_view key) const {
    const auto* value = find(key);
    return value != nullptr && value->is_object() ? *value : nlohmann::json::object();
}

nlohmann::json ToolArgs::missing(std::string_view key) const {
    return ToolResult::error(std::string{tool_} + ": missing " + std::string{key});
}

}  // namespace lsw::host::mcp
