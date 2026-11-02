#pragma once

#include <QString>
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
        [[nodiscard]] bool operator==(const GuestStep&) const = default;
    };

    State state = State::Idle;
    QString step;
    QString message;
    QString shipped_release;
    QString guest_release;
    QString shipped_version;
    QString guest_version;
    QVector<GuestStep> last_steps;

    [[nodiscard]] bool running() const noexcept { return state == State::Running; }
    [[nodiscard]] bool operator==(const GuestUpdateStatus&) const = default;
};

}
