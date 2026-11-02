#include "lsw/host/mcp/tools/guest_update_tool.hpp"

#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"
#include "lsw/host/services/guest_update.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QMetaObject>
#include <QObject>

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace lsw::host::mcp {

namespace {
constexpr auto kPoll = std::chrono::seconds(1);

}

std::string GuestUpdateTool::name() const { return "guest_update"; }

std::string GuestUpdateTool::description() const {
    return "Run the manager's update of Windows (the same as Settings, \"Update LSW "
           "in Windows\"): send the release's files and run lsw-guest-update.ps1 in "
           "the guest, then wait for the run to end. ``force`` (default true) "
           "replaces every file and runs every step. The guest may restart. "
           "Returns the outcome (succeeded or failed, with the message) and the "
           "guest's steps; an error when a run is already going. Blocks for up to "
           "about 15 minutes.";
}

nlohmann::json GuestUpdateTool::input_schema() const {
    return JsonSchema::object({{"force", {{"type", "boolean"}, {"default", true}}}});
}

nlohmann::json GuestUpdateTool::call(const nlohmann::json& args) {
    const bool force = args.is_object() && args.contains("force") && args["force"].is_boolean()
                           ? args["force"].get<bool>()
                           : true;

    bool started = false;
    QMetaObject::invokeMethod(&update_, [&] {
        if (!update_.running()) {
            update_.run(force);
            started = true;
        }
    }, Qt::BlockingQueuedConnection);
    if (!started) {
        return ToolResult::error("An update of Windows is already running.");
    }

    services::GuestUpdateStatus last;
    for (;;) {
        std::this_thread::sleep_for(kPoll);
        QMetaObject::invokeMethod(&update_, [&] { last = status_.update(); },
                                  Qt::BlockingQueuedConnection);
        if (!last.running()) {
            break;
        }
    }

    nlohmann::json steps = nlohmann::json::array();
    for (const auto& step : last.last_steps) {
        steps.push_back({{"name", step.name.toStdString()},
                         {"status", step.status.toStdString()},
                         {"message", step.message.toStdString()}});
    }
    const bool ok = last.state == services::GuestUpdateStatus::State::Succeeded;
    nlohmann::json body = {{"outcome", ok ? "succeeded" : "failed"},
                           {"message", last.message.toStdString()},
                           {"forced", force},
                           {"shipped_release", last.shipped_release.toStdString()},
                           {"last_steps", steps}};
    return ToolResult::text(body.dump(2), !ok);
}

}
