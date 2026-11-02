#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace lsw::host::platform {
struct MonitorRates {
    QString name;
    QVector<qint64> millihz;
};

struct RateChoice {
    qint64 millihz = 0;
    QStringList monitors;
};

class HostMonitorRates {
public:
    static constexpr qint64 kSameChoiceMillihz = 500;

    HostMonitorRates() = delete;

    [[nodiscard]] static qint64 mode_millihz(quint32 clock_khz, quint32 htotal, quint32 vtotal);

    [[nodiscard]] static QVector<MonitorRates> read();

    [[nodiscard]] static QVector<RateChoice> merge(const QVector<MonitorRates>& monitors,
                                                   const QVector<MonitorRates>& screens);
};

}
