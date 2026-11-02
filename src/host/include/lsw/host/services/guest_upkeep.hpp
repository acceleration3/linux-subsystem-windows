#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class GuestUpdate;
class StatusHub;

// Keeps LSW's half inside Windows as current as the LSW running it, for
// guests set up by any release.
//
// Once each boot, when the agent answers - the guest booted, or the
// manager started beside a running one - and setup has finished, which
// the machine's definition says (no install-time VGA; VmRecord's
// first_boot_done is not kept across runs of the manager):
//   1. the release the agent reports (system_info) is compared with the one
//      this manager ships, and GuestUpdate brings the guest to it when they
//      differ: the agent, the mirror, the driver package, the loaders, the
//      shared folders. While it runs nothing else here touches the guest.
//      Not forced: the script replaces only what differs.
//   2. Windows is given the host's time zone and, when its clock is off,
//      the host's time (the agent's set_clock): the VM's RTC runs on the
//      host's local time, which Windows reads in its own zone.
//
// The probe and the clock run on worker threads, through the agent's
// blocking calls; what they found is applied here.
class GuestUpkeep : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,      // nothing to do yet: no agent, or setup has not finished
        Checking,  // asking Windows for its release, or setting the clock
        Updating,  // GuestUpdate is bringing Windows to the shipped release
        Current,   // done for this boot
        Failed,    // the update or the clock did not take: ``error`` says why
    };

    GuestUpkeep(StatusHub& status, link::AgentRpcClient& rpc, GuestUpdate& update,
                QObject* parent = nullptr);
    ~GuestUpkeep() override;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    // This boot's pass got as far as the shared folders being mounted, which
    // the update does. A pass that stopped before has not, and a document in
    // a shared folder is not to be opened as if it had.
    [[nodiscard]] bool shares_ready() const noexcept { return shares_reached_; }

    // A failed pass again, the update included.
    void retry();

Q_SIGNALS:
    void changed();

private:
    struct Probe {
        bool ok = false;
        QString release;
        QString version;
        QString error;
    };
    struct Clock {
        bool ok = true;
        QString error;
    };

    void on_status();
    // Whether this boot's guest is one to keep: answering, set up.
    [[nodiscard]] bool ready() const;
    // A step is going: a worker, or GuestUpdate's run for this pass.
    [[nodiscard]] bool busy() const;
    void start();
    // The machine stopped while the pass ran: drops it and starts over for
    // the next boot. True when it did.
    bool end_if_stopped();
    void finish_pass(bool ok, const QString& error);
    // True when it changed (and ``changed`` was emitted).
    bool set_state(State state);
    // The workers' steps.
    [[nodiscard]] Probe probe();
    [[nodiscard]] Clock set_clock();

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    GuestUpdate& update_;
    QFutureWatcher<Probe> probe_watcher_;
    QFutureWatcher<Clock> clock_watcher_;
    State state_ = State::Idle;
    QString error_;
    // A pass ran for this boot: the next is for the next one.
    bool passed_ = false;
    bool shares_reached_ = false;
    // GuestUpdate's run is this pass's step.
    bool awaiting_update_ = false;
    // The machine was seen to stop while a pass ran: that pass is about a boot
    // that is over, and the next one has not had its own.
    bool stopped_during_pass_ = false;
};

}  // namespace lsw::host::services
