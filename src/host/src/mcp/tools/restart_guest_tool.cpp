#include "lsw/host/mcp/tools/restart_guest_tool.hpp"

#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <QMetaObject>
#include <QObject>
#include <nlohmann/json.hpp>

namespace lsw::host::mcp {

std::string RestartGuestTool::name() const {
    return "restart_guest";
}

std::string RestartGuestTool::description() const {
    return "Shuts Windows down and starts it again the way the manager's restart does: the machine "
           "is "
           "redefined from the manager's settings first, so settings that wait for a restart take "
           "effect. "
           "Returns once the restart has begun, not when Windows is back; system_info answers "
           "then.";
}

nlohmann::json RestartGuestTool::input_schema() const {
    return JsonSchema::no_args();
}

nlohmann::json RestartGuestTool::call(const nlohmann::json&) {
    manager::RestartResult result = manager::RestartResult::Refused;
    QString reason;
    QMetaObject::invokeMethod(
        &main_thread_, [&] { result = restarter_(&reason); }, Qt::BlockingQueuedConnection);
    switch (result) {
    case manager::RestartResult::Started:
        return ToolResult::text("Windows is restarting.");
    case manager::RestartResult::Pending:
        return ToolResult::text(
            "A restart is already under way; the machine is redefined again after it.");
    case manager::RestartResult::Refused:
        break;
    }
    return ToolResult::error("The restart was refused: " + reason.toStdString());
}

}  // namespace lsw::host::mcp
