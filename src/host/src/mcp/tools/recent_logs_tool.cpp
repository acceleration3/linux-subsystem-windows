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
    return "Return the last N log records the agent has pushed to the manager. "
           "Useful when debugging — these are the same lines as in ``Open Log``. "
           "Args: ``count`` (default 100).";
}

nlohmann::json RecentLogsTool::input_schema() const {
    return JsonSchema::object({{"count", {{"type", "integer"}}}});
}

nlohmann::json RecentLogsTool::call(const nlohmann::json& args) {
    const auto count = static_cast<qsizetype>(std::max<std::int64_t>(
        0, ToolArgs{name(), args}.integer("count", 100)));
    const auto& records = log_store_.records();
    const qsizetype start = records.size() > count ? records.size() - count : 0;
    QString out;
    for (qsizetype i = start; i < records.size(); ++i) {
        const auto& r = records[i];
        const QDateTime dt =
            QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(r.ts_ms)).toLocalTime();
        out += QStringLiteral("[%1 %2] %3\n")
                   .arg(dt.toString(QStringLiteral("HH:mm:ss.zzz")),
                        r.level.leftJustified(5, QLatin1Char(' ')), r.msg);
    }
    return ToolResult::text(out.toStdString());
}

}  // namespace lsw::host::mcp
