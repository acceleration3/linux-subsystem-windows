#include "lsw/host/config/display_refresh.hpp"

#include <cmath>

namespace lsw::host::config {

std::optional<quint32> DisplayRefresh::fixed(const QString& setting) {
    bool ok = false;
    const qulonglong value = setting.trimmed().toULongLong(&ok);
    if (!ok || value < kLeastMillihz || value > kMostMillihz) {
        return std::nullopt;
    }
    return static_cast<quint32>(value);
}

std::optional<quint32> DisplayRefresh::from_hertz(double hertz) {
    if (!std::isfinite(hertz)) {
        return std::nullopt;
    }
    const double millihz = std::round(hertz * 1000.0);
    if (millihz < kLeastMillihz || millihz > kMostMillihz) {
        return std::nullopt;
    }
    return static_cast<quint32>(millihz);
}

std::optional<quint32> DisplayRefresh::resolve(const QString& setting,
                                               std::optional<quint32> monitor) {
    if (const auto own = fixed(setting)) {
        return own;
    }
    return monitor;
}

QString DisplayRefresh::text(quint32 millihz) {
    return QString::number(millihz);
}

QString DisplayRefresh::label(std::optional<quint32> millihz) {
    if (!millihz) {
        return QStringLiteral("-");
    }
    if (*millihz % 1000 == 0) {
        return QStringLiteral("%1 Hz").arg(*millihz / 1000);
    }
    QString decimals = QStringLiteral("%1").arg(*millihz % 1000, 3, 10, QLatin1Char('0'));
    while (decimals.endsWith(QLatin1Char('0'))) {
        decimals.chop(1);
    }
    return QStringLiteral("%1.%2 Hz").arg(*millihz / 1000).arg(decimals);
}

}
