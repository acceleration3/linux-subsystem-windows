#include "lsw/host/mcp/tools/console_read_tool.hpp"

#include "lsw/host/mcp/console_session_buffer.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

#include <QString>

namespace lsw::host::mcp {

std::string ConsoleReadTool::name() const { return "console_read"; }

std::string ConsoleReadTool::description() const {
    return "Return + clear the accumulated stdout/stderr bytes for ``session_id``. "
           "Empty string if nothing has been received yet; call again after a "
           "moment if you just sent input.";
}

nlohmann::json ConsoleReadTool::input_schema() const {
    return JsonSchema::object({{"session_id", {{"type", "string"}}}}, {"session_id"});
}

nlohmann::json ConsoleReadTool::call(const nlohmann::json& args) {
    const ToolArgs in{name(), args};
    const auto session_id = QString::fromStdString(in.string("session_id"));
    if (session_id.isEmpty()) {
        return in.missing("session_id");
    }
    return ToolResult::text(QString::fromUtf8(buffer_.take(session_id)).toStdString());
}

}  // namespace lsw::host::mcp
