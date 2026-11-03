#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace lsw::host::platform {
struct HostMonitor {
    QString name;
    QVector<qint64> millihz;
    QSize mode;

    bool operator==(const HostMonitor&) const = default;
};

struct RateChoice {
    qint64 millihz = 0;
    QStringList monitors;
};

class HostMonitors {
public:
    static constexpr qint64 kSameChoiceMillihz = 500;

    HostMonitors() = delete;

    [[nodiscard]] static qint64 mode_millihz(quint32 clock_khz, quint32 htotal, quint32 vtotal);

    [[nodiscard]] static QVector<HostMonitor> read();

    [[nodiscard]] static QVector<RateChoice> merge(const QVector<HostMonitor>& monitors,
                                                   const QVector<HostMonitor>& screens);
};

}
