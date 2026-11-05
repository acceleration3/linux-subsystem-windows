#include "lsw/host/services/host_window_alerts.hpp"

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
    if (w.full_since_ms && now - *w.full_since_ms >= kFullHoldMs) {
        const auto top = w.holders.isEmpty() ? nullptr : &w.holders.first();
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
    for (const auto& transfer : w.transfers) {
        if (recent(transfer.refused_at_ms, now)) {
            named = true;
            out.push_back({Warning::Kind::Refusals, true,
                           QStringLiteral("%1 is being refused host window memory.")
                               .arg(transfer.label()),
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
    for (const auto& transfer : w.transfers) {
        if (transfer.hot_split_ms >= kHotHoldMs) {
            out.push_back({Warning::Kind::SplitTransfers, false,
                           QStringLiteral("%1's transfers need several trips through the "
                                          "host window.")
                               .arg(transfer.label()),
                           QStringLiteral("More than 5 transfers a second have been sent in "
                                          "pieces for 10 seconds, because the window had no "
                                          "room for each one whole.")});
        }
    }
    for (const auto& transfer : w.transfers) {
        if (recent(transfer.copied_at_ms, now)) {
            out.push_back({Warning::Kind::CopiedMappings, false,
                           QStringLiteral("%1's dynamic buffers are copied instead of drawn "
                                          "in place.")
                               .arg(transfer.label()),
                           QStringLiteral("The app keeps rewriting buffers the host cannot "
                                          "share directly, so every change is copied.")});
        }
    }
    for (const auto& transfer : w.transfers) {
        if (recent(transfer.arena_full_at_ms, now)) {
            out.push_back({Warning::Kind::ArenaFull, false,
                           QStringLiteral("%1's transfer arena is nearly full, so its uploads "
                                          "wait for room.")
                               .arg(transfer.label()),
                           QStringLiteral("Its transfer ring has reached 90% of the arena, and "
                                          "uploads have been waiting for room.")});
        }
    }
    return out;
}

QVector<const HostWindowStatus::Transfer*> HostWindowAlerts::shown_transfers(const HostWindowStatus& status) {
    QVector<const HostWindowStatus::Transfer*> shown;
    for (const auto& transfer : status.transfers) {
        const auto peak = transfer.arena_peak_percent();
        if (transfer.active || (peak && *peak >= kArenaShownPercent)) {
            shown.push_back(&transfer);
        }
    }
    return shown;
}

}
