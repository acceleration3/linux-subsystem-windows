#include "lsw/host/services/host_window_alerts.hpp"

#include <algorithm>

namespace lsw::host::services {

namespace {

bool recent(const std::optional<qint64>& at, qint64 now) {
    return at.has_value() && now - *at <= HostWindowAlerts::kRiseShownMs;
}

}

QVector<HostWindowAlerts::Warning> HostWindowAlerts::warnings(const HostWindowStatus& w, qint64 now) {
    QVector<Warning> out;
    if (!w.read) {
        return out;
    }
    QVector<const HostWindowStatus::App*> apps;
    for (const auto& app : w.apps) {
        apps.push_back(&app);
    }
    std::sort(apps.begin(), apps.end(), [](const HostWindowStatus::App* a, const HostWindowStatus::App* b) {
        return a->device != b->device ? a->device < b->device : a->context < b->context;
    });
    if (w.full_since_ms && now - *w.full_since_ms >= kFullHoldMs) {
        const HostWindowStatus::App* top = nullptr;
        for (const auto* app : apps) {
            if (app->held_bytes > 0 && (top == nullptr || app->held_bytes > top->held_bytes)) {
                top = app;
            }
        }
        out.push_back({Warning::Kind::WindowFull, false,
                       top != nullptr
                           ? QStringLiteral("The host window is over 90% full, mostly with %1.")
                                 .arg(top->label())
                           : QStringLiteral("The host window is over 90% full."),
                       QStringLiteral("It has stayed that full for several seconds. When it "
                                      "runs out, allocations are refused and the apps see "
                                      "out-of-memory errors.")});
    }
    bool named = false;
    for (const auto* app_ptr : apps) {
        const auto& app = *app_ptr;
        if (recent(app.refused_at_ms, now)) {
            named = true;
            out.push_back({Warning::Kind::Refusals, true,
                           QStringLiteral("%1 is being refused host window memory.")
                               .arg(app.label()),
                           QStringLiteral("The app sees out-of-memory errors. The host window "
                                          "has no room for what it asks for.")});
        }
    }
    if (!named && recent(w.window_refused_at_ms, now)) {
        out.push_back({Warning::Kind::Refusals, true,
                       QStringLiteral("The host window is refusing allocations."),
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

}
