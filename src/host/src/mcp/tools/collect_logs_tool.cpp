#include "lsw/host/mcp/tools/collect_logs_tool.hpp"

#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"
#include "lsw/host/services/log_collector.hpp"

#include <QMetaObject>

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace lsw::host::mcp {

namespace {

constexpr auto kPoll = std::chrono::milliseconds(500);
constexpr auto kLimit = std::chrono::minutes(5);

}

std::string CollectLogsTool::name() const { return "collect_logs"; }

std::string CollectLogsTool::description() const {
    return "Collect the logs for a bug report (the same as Settings, \"Collect logs for a bug "
           "report\"): the host logs folder, Windows' logs, system.txt and settings.txt in "
           "lsw-logs-<time>.tar.gz in the logs folder. Waits for it to finish. Returns the "
           "archive's path, and why Windows' logs are missing when they are.";
}

nlohmann::json CollectLogsTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json CollectLogsTool::call(const nlohmann::json&) {
    QMetaObject::invokeMethod(&collector_, [this] { collector_.start(); },
                              Qt::BlockingQueuedConnection);
    const auto deadline = std::chrono::steady_clock::now() + kLimit;
    bool running = true;
    while (running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPoll);
        QMetaObject::invokeMethod(&collector_, [&] { running = collector_.running(); },
                                  Qt::BlockingQueuedConnection);
    }
    if (running) {
        return ToolResult::error("Collecting the logs is taking longer than 5 minutes.");
    }
    nlohmann::json body;
    bool ok = false;
    QMetaObject::invokeMethod(&collector_, [&] {
        ok = collector_.error().isEmpty() && !collector_.archive().isEmpty();
        body = {{"archive", collector_.archive().toStdString()},
                {"error", collector_.error().toStdString()},
                {"windows_logs", collector_.windows_note().isEmpty()
                                     ? std::string{"included"}
                                     : collector_.windows_note().toStdString()}};
    }, Qt::BlockingQueuedConnection);
    return ToolResult::text(body.dump(2), !ok);
}

}
