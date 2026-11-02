#include "lsw/host/mcp/tools/recent_logs_tool.hpp"

#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_args.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

#include <QDateTime>
#include <QString>

#include <algorithm>
#include <cstdint>

namespace lsw::host::mcp {

std::string RecentLogsTool::name() const { return "recent_logs"; }

std::string RecentLogsTool::description() const {
    return "Return the last N log records the manager holds: its own, Qt's, the "
           "agent's and each window mirror's, the same lines as the Log page. "
           "Args: ``count`` (default 100); ``source`` (optional) keeps one "
           "source: \"manager\", \"qt\", \"agent\" (its sessions too), "
           "\"mirror\" (all mirrors) or an exact one such as \"mirror notepad\".";
}

nlohmann::json RecentLogsTool::input_schema() const {
    return JsonSchema::object(
        {{"count", {{"type", "integer"}}}, {"source", {{"type", "string"}}}});
}

nlohmann::json RecentLogsTool::call(const nlohmann::json& args) {
    const auto count = static_cast<qsizetype>(std::max<std::int64_t>(
        0, ToolArgs{name(), args}.integer("count", 100)));
    const QString source = QString::fromStdString(ToolArgs{name(), args}.string("source"));
    const auto records = log_store_.recent(static_cast<int>(std::min<qsizetype>(count, 1000000)), source);
    QString out;
    for (const auto& r : records) {
        const QDateTime dt =
            QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(r.ts_ms)).toLocalTime();
        out += QStringLiteral("[%1 %2 %3] %4\n")
                   .arg(dt.toString(QStringLiteral("HH:mm:ss.zzz")),
                        r.level.leftJustified(5, QLatin1Char(' ')), r.source, r.msg);
    }
    return ToolResult::text(out.toStdString());
}

}
