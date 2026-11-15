#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class GraphicsAlerts : public QObject {
    Q_OBJECT
public:
    struct Warning {
        enum class Kind {
            NoDirect3d,
            ResizableBarOff,
        };
        Kind kind = Kind::NoDirect3d;
        bool critical = false;
        QString headline;
        QString detail;
    };

    enum class Severity { None, Amber, Red };

    explicit GraphicsAlerts(services::StatusHub& status, QObject* parent = nullptr);

    [[nodiscard]] QVector<Warning> warnings() const;
    [[nodiscard]] Severity worst() const;

Q_SIGNALS:
    void changed();

private:
    services::StatusHub& status_;
};

}
