#pragma once

#include "lsw/host/services/guest_update_status.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QString>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class GuestUpdate;
class StatusHub;

class GuestUpkeep : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Checking,
        Updating,
        Current,
        Failed,
    };

    GuestUpkeep(StatusHub& status, link::AgentRpcClient& rpc, GuestUpdate& update,
                QObject* parent = nullptr);
    ~GuestUpkeep() override;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    [[nodiscard]] bool shares_ready() const noexcept { return shares_reached_; }

    void retry();

    void disable() noexcept { enabled_ = false; }

Q_SIGNALS:
    void changed();

private:
    struct Probe {
        bool ok = false;
        QString release;
        QString config;
        QString version;
        QVector<GuestUpdateStatus::GuestStep> steps;
        QString finished;
        bool last_ok = false;
        QString error;
    };
    struct Clock {
        bool ok = true;
        QString error;
    };

    void on_status();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool busy() const;
    void start();
    bool end_if_stopped();
    void finish_pass(bool ok, const QString& error);
    bool set_state(State state);
    [[nodiscard]] Probe probe();
    [[nodiscard]] Clock set_clock();

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    GuestUpdate& update_;
    QFutureWatcher<Probe> probe_watcher_;
    QFutureWatcher<Clock> clock_watcher_;
    State state_ = State::Idle;
    QString error_;
    bool enabled_ = true;
    bool passed_ = false;
    bool shares_reached_ = false;
    bool awaiting_update_ = false;
    bool stopped_during_pass_ = false;
};

}
