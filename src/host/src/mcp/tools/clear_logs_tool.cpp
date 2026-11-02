#include "lsw/host/mcp/tools/clear_logs_tool.hpp"

#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"
#include "lsw/host/services/log_cleaner.hpp"

#include <QMetaObject>

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace lsw::host::mcp {

namespace {

constexpr auto kPoll = std::chrono::milliseconds(200);
constexpr auto kLimit = std::chrono::minutes(2);

}

std::string ClearLogsTool::name() const { return "clear_logs"; }

std::string ClearLogsTool::description() const {
    return "Clear all logs (the same as Settings, \"Clear all logs\"): the host logs folder, the "
           "Log page and Windows' logs. Waits for it to finish. Returns what was removed and "
           "emptied on each side, and what could not be cleared.";
}

nlohmann::json ClearLogsTool::input_schema() const { return JsonSchema::no_args(); }

nlohmann::json ClearLogsTool::call(const nlohmann::json&) {
    QMetaObject::invokeMethod(&cleaner_, [this] { cleaner_.start(); }, Qt::BlockingQueuedConnection);
    const auto deadline = std::chrono::steady_clock::now() + kLimit;
    bool running = true;
    while (running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPoll);
        QMetaObject::invokeMethod(&cleaner_, [&] { running = cleaner_.running(); },
                                  Qt::BlockingQueuedConnection);
    }
    if (running) {
        return ToolResult::error("Clearing the logs is taking longer than 2 minutes.");
    }
    nlohmann::json body;
    QMetaObject::invokeMethod(&cleaner_, [&] {
        const auto& result = cleaner_.result();
        body = {{"headline", result.headline().toStdString()},
                {"detail", result.detail().toStdString()},
                {"host", {{"removed", result.removed},
                          {"emptied", result.emptied}}},
                {"windows", {{"cleared", result.windows_cleared},
                             {"note", result.windows_note.toStdString()},
                             {"removed", result.windows_removed},
                             {"emptied", result.windows_emptied}}}};
        for (const QString& path : result.failed) {
            body["host"]["failed"].push_back(path.toStdString());
        }
        for (const QString& path : result.windows_skipped) {
            body["windows"]["skipped"].push_back(path.toStdString());
        }
    }, Qt::BlockingQueuedConnection);
    return ToolResult::text(body.dump(2));
}

}
