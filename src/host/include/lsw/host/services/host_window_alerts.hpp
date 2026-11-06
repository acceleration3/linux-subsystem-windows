#pragma once

#include "lsw/host/services/host_window_status.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::services {

class HostWindowAlerts {
public:
    struct Warning {
        enum class Kind { WindowFull, Refusals, SplitTransfers, CopiedMappings, ArenaFull };
        Kind kind = Kind::WindowFull;
        bool critical = false;
        QString headline;
        QString detail;
    };

    static constexpr qint64 kFullHoldMs = 5000;
    static constexpr qint64 kHotHoldMs = 10'000;
    static constexpr qint64 kRiseShownMs = 60'000;

    HostWindowAlerts() = delete;

    [[nodiscard]] static QVector<Warning> warnings(const HostWindowStatus& status, qint64 now_ms);
};

}
