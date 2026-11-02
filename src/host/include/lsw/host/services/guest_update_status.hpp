#pragma once

#include <QString>
#include <QVector>

namespace lsw::host::services {

// Updating LSW in Windows as StatusHub last heard of it (GuestUpdate
// publishes it): whether a run is going and which step the host is on, how
// the last one ended, and the two releases the next one is decided by.
struct GuestUpdateStatus {
    enum class State {
        Idle,       // no run since the manager started
        Running,    // ``step`` says what the host is doing
        Failed,     // ``message`` says why
        Succeeded,  // the guest reports the shipped release
    };

    // One step of the guest's script, as its update-result.json says it.
    struct GuestStep {
        QString name;
        // done, skipped, failed or not run.
        QString status;
        QString message;
        [[nodiscard]] bool operator==(const GuestStep&) const = default;
    };

    State state = State::Idle;
    // While Running: the host's step, a phrase for the status bar.
    QString step;
    // While Failed: why, with where the guest's log is.
    QString message;
    // What this manager would put in the guest; empty until built.
    QString shipped_release;
    // What the guest's agent last reported; empty when it reports none, or
    // has not been asked.
    QString guest_release;
    // The LSW versions of those two, for naming them; empty when unknown.
    QString shipped_version;
    QString guest_version;
    // The guest's steps in the last run of this manager's making, once it
    // has ended; empty before, and when the guest did not say.
    QVector<GuestStep> last_steps;

    [[nodiscard]] bool running() const noexcept { return state == State::Running; }
    [[nodiscard]] bool operator==(const GuestUpdateStatus&) const = default;
};

}  // namespace lsw::host::services
