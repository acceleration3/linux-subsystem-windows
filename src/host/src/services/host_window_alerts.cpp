#include "lsw/host/services/host_window_alerts.hpp"

#include <algorithm>

namespace lsw::host::services {

namespace {

bool recent(const std::optional<qint64>& at, qint64 now) {
    return at.has_value() && now - *at <= HostWindowAlerts::kRiseShownMs;
}

}  // namespace

QVector<HostWindowAlerts::Warning> HostWindowAlerts::warnings(const HostWindowStatus& w, qint64 now,
                                                              const QStringList& gpu_names) {
    const auto window_of = [&](quint32 device) {
        if (w.windows.size() < 2) {
            return QStringLiteral("The host window");
        }
        const QString gpu = device < static_cast<quint32>(gpu_names.size()) &&
                                    !gpu_names.at(static_cast<qsizetype>(device)).isEmpty()
                                ? gpu_names.at(static_cast<qsizetype>(device))
                                : QStringLiteral("GPU %1").arg(device + 1);
        return QStringLiteral("The host window of %1").arg(gpu);
    };
    QVector<Warning> out;
    if (!w.read) {
        return out;
    }
    QVector<const HostWindowStatus::App*> apps;
    for (const auto& app : w.apps) {
        apps.push_back(&app);
    }
    std::sort(apps.begin(), apps.end(),
              [](const HostWindowStatus::App* a, const HostWindowStatus::App* b) {
                  return a->device != b->device ? a->device < b->device : a->context < b->context;
              });
    for (const auto& window : w.windows) {
        if (!window.full_since_ms || now - *window.full_since_ms < kFullHoldMs) {
            continue;
        }
        const HostWindowStatus::App* top = nullptr;
        for (const auto* app : apps) {
            if (app->device == window.device && app->held_bytes > 0 &&
                (top == nullptr || app->held_bytes > top->held_bytes)) {
                top = app;
            }
        }
        out.push_back({Warning::Kind::WindowFull, false,
                       top != nullptr
                           ? QStringLiteral("%1 is over 90% full, mostly with %2.")
                                 .arg(window_of(window.device), top->label())
                           : QStringLiteral("%1 is over 90% full.").arg(window_of(window.device)),
                       QStringLiteral("It has stayed that full for several seconds. When it "
                                      "runs out, allocations are refused and the apps see "
                                      "out-of-memory errors.")});
    }
    bool named = false;
    for (const auto* app_ptr : apps) {
        const auto& app = *app_ptr;
        if (recent(app.refused_at_ms, now)) {
            named = true;
            out.push_back(
                {Warning::Kind::Refusals, true,
                 QStringLiteral("%1 is being refused host window memory.").arg(app.label()),
                 QStringLiteral("The app sees out-of-memory errors. The host window "
                                "has no room for what it asks for.")});
        }
    }
    for (const auto& window : w.windows) {
        if (named || !recent(window.refused_at_ms, now)) {
            continue;
        }
        out.push_back({Warning::Kind::Refusals, true,
                       QStringLiteral("%1 is refusing allocations.").arg(window_of(window.device)),
                       QStringLiteral("An app sees out-of-memory errors. The host window has "
                                      "no room for what it asks for.")});
    }
    for (const auto* app_ptr : apps) {
        const auto& app = *app_ptr;
        if (app.hot_split_ms >= kHotHoldMs) {
            out.push_back({Warning::Kind::SplitTransfers, false,
                           QStringLiteral("%1's transfers need several trips through the "
                                          "host window.")
                               .arg(app.label()),
                           QStringLiteral("More than 5 transfers a second have been sent in "
                                          "pieces for 10 seconds, because the window had no "
                                          "room for each one whole.")});
        }
    }
    for (const auto* app_ptr : apps) {
        const auto& app = *app_ptr;
        if (recent(app.copied_at_ms, now)) {
            out.push_back({Warning::Kind::CopiedMappings, false,
                           QStringLiteral("%1's dynamic buffers are copied instead of drawn "
                                          "in place.")
                               .arg(app.label()),
                           QStringLiteral("The app keeps rewriting buffers the host cannot "
                                          "share directly, so every change is copied.")});
        }
    }
    for (const auto* app_ptr : apps) {
        const auto& app = *app_ptr;
        if (recent(app.arena_full_at_ms, now)) {
            out.push_back({Warning::Kind::ArenaFull, false,
                           QStringLiteral("%1's transfer arena is nearly full, so its uploads "
                                          "wait for room.")
                               .arg(app.label()),
                           QStringLiteral("Its transfer ring has reached 90% of the arena, and "
                                          "uploads have been waiting for room.")});
        }
    }
    return out;
}

}  // namespace lsw::host::services
