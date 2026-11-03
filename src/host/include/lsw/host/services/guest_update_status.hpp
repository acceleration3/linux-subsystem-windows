#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::services {
struct GuestUpdateStatus {
    enum class State {
        Idle,
        Running,
        Failed,
        Succeeded,
    };

    struct GuestStep {
        QString name;
        QString status;
        QString message;
        bool restart = false;
        [[nodiscard]] bool operator==(const GuestStep&) const = default;
    };

    enum class Restart {
        Pending,
        Running,
        Done,
        NotNeeded,
    };

    State state = State::Idle;
    QString step;
    QString message;
    QString shipped_release;
    QString guest_release;
    QString shipped_config;
    QString guest_config;
    QString shipped_version;
    QString guest_version;
    QVector<GuestStep> last_steps;
    QVector<GuestStep> live_steps;
    Restart restart = Restart::Pending;
    QStringList restart_reasons;
    bool quiet = false;
    QString finished;
    bool last_ok = false;

    [[nodiscard]] bool running() const noexcept { return state == State::Running; }
    [[nodiscard]] bool operator==(const GuestUpdateStatus&) const = default;
};

}
