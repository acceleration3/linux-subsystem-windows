#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QString>

#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class StatusHub;

// Keeps LSW's half inside Windows as current as the LSW running it, for
// guests set up by any release.
//
// Once each boot, when the agent answers - the guest booted, or the
// manager started beside a running one - and setup has finished, which
// the machine's definition says (no install-time VGA; VmRecord's
// first_boot_done is not kept across runs of the manager):
//   1. the agent and the window mirror's guest half are compared with the
//      ones LSW ships (the agent's file_hashes), and swapped for them when
//      either differs (AgentRedeployer). An agent too old to know
//      file_hashes is older than this, so it is swapped too. A pair of
//      shipped binaries is tried once per run of the manager, so a swap
//      that keeps failing does not loop; ``retry`` tries it again.
//   2. Windows is given the host's time zone and, when its clock is off,
//      the host's time (the agent's set_clock): the VM's RTC runs on the
//      host's local time, which Windows reads in its own zone.
// The driver package is not part of this: it changes the display driver,
// so it waits for the user on the Graphics page (GraphicsAlerts).
//
// The pass runs on a worker thread, through the agent's blocking calls;
// what it found is applied here.
class GuestUpkeep : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,      // nothing to do yet: no agent, or setup has not finished
        Checking,  // comparing, or setting the clock
        Updating,  // swapping the agent and the mirror's guest half
        Current,   // done for this boot
        Failed,    // the swap or the clock did not take: ``error`` says why
    };

    GuestUpkeep(StatusHub& status, link::AgentRpcClient& rpc, QObject* parent = nullptr);
    ~GuestUpkeep() override;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }

    // A failed pass again, the swap included.
    void retry();

Q_SIGNALS:
    void changed();

private:
    struct Result {
        bool ok = true;
        QString error;
        // The shipped pair a swap was tried for, to try once.
        QString attempted;
    };

    void on_status();
    // Whether this boot's guest is one to keep: answering, set up.
    [[nodiscard]] bool ready() const;
    void start();
    void set_state(State state);
    // The worker's pass: compare, swap, clock.
    [[nodiscard]] Result pass(QSet<QString> attempted);

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    QFutureWatcher<Result> watcher_;
    State state_ = State::Idle;
    QString error_;
    // A pass ran for this boot: the next is for the next one.
    bool passed_ = false;
    // Shipped pairs (their hashes) a swap was tried for in this run.
    QSet<QString> attempted_;
};

}  // namespace lsw::host::services
