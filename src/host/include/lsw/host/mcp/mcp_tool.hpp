#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::host::mcp {

// One thing an MCP client can ask the manager to do.
//
// A tool is its own class, in its own file, because a tool is the unit
// this server grows by: adding one should be adding a file, not editing
// a switch, a schema list, and a dispatcher in three places.
class IMcpTool {
public:
    IMcpTool() = default;
    virtual ~IMcpTool() = default;
    IMcpTool(const IMcpTool&) = delete;
    IMcpTool& operator=(const IMcpTool&) = delete;

    // Wire name, as it appears in ``tools/list`` and ``tools/call``.
    [[nodiscard]] virtual std::string name() const = 0;

    // Prose for the model. Say what it returns and what to call next —
    // this text is the entire interface as far as the model is concerned.
    [[nodiscard]] virtual std::string description() const = 0;

    // JSON Schema for the arguments. Build it with JsonSchema.
    [[nodiscard]] virtual nlohmann::json input_schema() const = 0;

    // False for tools that work with no VM targeted (e.g. reading logs
    // the manager already holds). The router gates on this so tools do
    // not each re-implement the unreachable-agent answer.
    [[nodiscard]] virtual bool requires_agent() const { return true; }

    // Run it. Blocking; called on an HTTP worker thread. Return a body
    // built with ToolResult.
    [[nodiscard]] virtual nlohmann::json call(const nlohmann::json& args) = 0;
};

}  // namespace lsw::host::mcp
