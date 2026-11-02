#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

// What is wrong with the guest's graphics, worded, from the status hub's
// snapshot. The driver itself is not offered here: LSW's update of Windows
// (services::GuestUpdate) brings it, on the next boot after a host GPU
// appears or the package changes, or from Settings.
//
// The Graphics page shows every warning; the overview the ones that break
// the guest; the rail a dot in the worst one's colour. One list, so the
// three agree.
class GraphicsAlerts : public QObject {
    Q_OBJECT
public:
    struct Warning {
        enum class Kind {
            NoDirect3d,
            DisplayNotRunning,
            // The display adapter with no driver at all (problem code 28):
            // the machine's GPU was switched since the install, and the
            // adapter wears the new GPU's PCI ID.
            DisplayNoDriver,
            NoDriver,
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
    // The machine as the last reading had it, to act on its transitions.
    bool running_ = false;
    bool agent_online_ = false;
};

}  // namespace lsw::host::manager
