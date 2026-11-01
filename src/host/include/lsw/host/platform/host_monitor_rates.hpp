#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace lsw::host::platform {

// What one connected monitor can run at, read from the kernel's own mode
// list for its connector: every progressive mode's exact rate, in
// millihertz, highest first.
struct MonitorRates {
    QString name;  // the connector, "DP-2"
    QVector<qint64> millihz;
};

// One refresh rate the guest could be given, and the monitors that
// support it. Rates within half a hertz of each other are one choice
// (59.94 and 60, 239.76 and 240), shown as the highest of them.
struct RateChoice {
    qint64 millihz = 0;
    QStringList monitors;
};

// The refresh rates of the monitors on this host, for the settings'
// refresh-rate choice: libdrm's connectors (no master needed, nothing
// probed or changed), and the pure rules that turn them into a list.
class HostMonitorRates {
public:
    // Rates this close are one choice.
    static constexpr qint64 kSameChoiceMillihz = 500;

    HostMonitorRates() = delete;

    // A mode's exact rate: its pixel clock (kHz) over its whole frame,
    // rounded to the millihertz. 0 for a mode with no frame.
    [[nodiscard]] static qint64 mode_millihz(quint32 clock_khz, quint32 htotal, quint32 vtotal);

    // Every connected connector of every /dev/dri/card* that can be
    // opened, with its non-interlaced, non-double-scan modes' rates. A node
    // that cannot be opened (no permission) is left out, not an error: the
    // answer is what could be read, which may be nothing. Blocks on the
    // kernel for a moment; callers run it off the GUI thread.
    [[nodiscard]] static QVector<MonitorRates> read();

    // The monitors' rates as choices, highest first: ``monitors`` and the
    // desktop's own screens (``screens``, whose one rate each is what the
    // compositor says now) together, so the current rate of every screen
    // is always offered. A monitor is named once however many of its modes
    // are within half a hertz of the choice.
    [[nodiscard]] static QVector<RateChoice> merge(const QVector<MonitorRates>& monitors,
                                                   const QVector<MonitorRates>& screens);
};

}  // namespace lsw::host::platform
