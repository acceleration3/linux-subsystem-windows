#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class GpuStatusAlerts : public QObject {
    Q_OBJECT
public:
    struct Warning {
        enum class Kind {
            DisplayNotRunning,
            DisplayNoDriver,
            NoDriver,
            WindowFull,
            Refusals,
            SplitTransfers,
            CopiedMappings,
            ArenaFull,
        };
        Kind kind = Kind::DisplayNotRunning;
        bool critical = false;
        QString headline;
        QString detail;
    };

    enum class Severity { None, Amber, Red };

    explicit GpuStatusAlerts(services::StatusHub& status, QObject* parent = nullptr);

    [[nodiscard]] QVector<Warning> warnings() const;
    [[nodiscard]] Severity worst() const;

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QString window_signature() const;

    services::StatusHub& status_;
    QString window_signature_;
    bool running_ = false;
    bool agent_online_ = false;
};

}
