#include "lsw/host/manager/gpu_status_alerts.hpp"

#include "lsw/host/services/status_hub.hpp"

#include <QStringList>

namespace lsw::host::manager {

namespace {

using Area = services::StatusHub::Area;

constexpr qint64 kFullHoldMs = 5000;
constexpr qint64 kHotHoldMs = 10'000;
constexpr qint64 kRiseShownMs = 60'000;

bool recent(const std::optional<qint64>& at, qint64 now) {
    return at.has_value() && now - *at <= kRiseShownMs;
}

}

GpuStatusAlerts::GpuStatusAlerts(services::StatusHub& status, QObject* parent)
    : QObject(parent), status_(status) {
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        const bool running = status_.vm().running();
        const bool online = status_.vm().agent_online();
        if (online != agent_online_ || running != running_) {
            agent_online_ = online;
            running_ = running;
            Q_EMIT changed();
            return;
        }
        if (area == Area::Graphics) {
            Q_EMIT changed();
        } else if (area == Area::HostWindow) {
            const QString signature = window_signature();
            if (signature != window_signature_) {
                window_signature_ = signature;
                Q_EMIT changed();
            }
        }
    });
}

QString GpuStatusAlerts::window_signature() const {
    QStringList parts;
    for (const auto& warning : warnings()) {
        if (warning.kind == Warning::Kind::WindowFull || warning.kind == Warning::Kind::Refusals
            || warning.kind == Warning::Kind::SplitTransfers
            || warning.kind == Warning::Kind::CopiedMappings || warning.kind == Warning::Kind::ArenaFull) {
            parts << warning.headline << warning.detail << QString::number(warning.critical);
        }
    }
    return parts.join(QLatin1Char('\x1f'));
}

QVector<GpuStatusAlerts::Warning> GpuStatusAlerts::warnings() const {
    const auto& g = status_.graphics();
    const bool running = status_.vm().running();
    QVector<Warning> out;
    if (g.guest.valid && running) {
        const services::GraphicsAdapter* display =
            g.adapters.isEmpty() ? nullptr : &g.adapters.first();
        if (display == nullptr || !display->guest) {
            out.push_back({Warning::Kind::DisplayNotRunning, true,
                           QStringLiteral("Windows is not running avgpu.sys."),
                           QStringLiteral("Windows lists no accel-virt adapter at all.")});
        } else if (display->guest->no_driver()) {
            out.push_back({Warning::Kind::DisplayNoDriver, true,
                           QStringLiteral("Windows has no driver for %1.")
                               .arg(platform::HostGpus::adapter_name(display->gpu)),
                           QStringLiteral("The machine's GPU was switched after the driver package "
                                          "was installed, and the adapter carries the new GPU's "
                                          "PCI ID, so Windows has not matched it to avgpu.sys "
                                          "yet.")});
        } else if (!display->guest->running()) {
            const quint32 code = display->guest->problem_code;
            out.push_back(
                {Warning::Kind::DisplayNotRunning, true,
                 QStringLiteral("Windows is not running avgpu.sys."),
                 code != 0 ? QStringLiteral("The adapter reports problem code %1: %2.")
                                 .arg(code)
                                 .arg(services::GuestGpuStatus::explain(code, g.guest.test_signing))
                           : QStringLiteral("The adapter has not started.")});
        }
    }
    if (g.guest.valid && running) {
        for (const auto& adapter : g.adapters) {
            if (!adapter.display && (!adapter.guest || adapter.guest->no_driver())) {
                out.push_back({Warning::Kind::NoDriver, false,
                               QStringLiteral("Windows has no driver for %1.")
                                   .arg(platform::HostGpus::adapter_name(adapter.gpu)),
                               QStringLiteral("It was added as an adapter after the driver "
                                              "package was installed, so Windows has not matched "
                                              "it to avgpu.sys yet. LSW's next update of "
                                              "Windows, at the next boot, installs the driver "
                                              "for it.")});
                break;
            }
        }
    }
    const auto& w = status_.host_window();
    if (running && w.read) {
        const qint64 now = w.at_ms;
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
                                              "wait for room")
                                   .arg(transfer.label()),
                               QStringLiteral("Its transfer ring has reached 90% of the arena, and "
                                              "uploads have been waiting for room.")});
            }
        }
    }
    return out;
}

GpuStatusAlerts::Severity GpuStatusAlerts::worst() const {
    Severity out = Severity::None;
    for (const auto& warning : warnings()) {
        if (warning.critical) {
            return Severity::Red;
        }
        out = Severity::Amber;
    }
    return out;
}

}
