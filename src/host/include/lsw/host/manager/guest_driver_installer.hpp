#pragma once

#include "lsw/host/link/event_subscription.hpp"

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Installs avgpu.sys for adapters Windows has no driver for.
//
// The driver package is signed for the PCI IDs present when Windows was
// installed, so a host GPU passed to the guest later is an adapter with
// problem code 28. Fixing it is accel-virt's own provision.ps1: sign the
// package for the IDs present now, then install it. That runs in the
// guest as an agent script job, whose progress and end come back as
// setup_progress and setup_done events like any setup step.
class GuestDriverInstaller : public QObject {
    Q_OBJECT
public:
    explicit GuestDriverInstaller(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    // The script, once, here. It signs a fresh copy because provision.ps1
    // -Stage sign first checks the package against its package.sha256,
    // and the first signing invalidated that: it rewrote avgpu.inf for
    // the IDs of the day and signed avgpu.sys. Setup keeps the package
    // as shipped in C:\av\pristine; it is mirrored to C:\av\resign and
    // signed and installed from there. 3010 from either stage is
    // "done, restart Windows", passed back as the script's exit code.
    [[nodiscard]] static QString script();

    // Start it. False, with ``failed`` emitted, when the agent would not.
    bool start();
    [[nodiscard]] bool running() const noexcept { return awaiting_id_ || !job_.isEmpty(); }

    // The agent went away with the job in flight: its end will never be
    // reported, so it ends here, as a failure that says why.
    void abandon(const QString& reason);

    // One setup_progress or setup_done event, as the reader thread hands
    // it over. Public for the test that replays a fast job.
    struct Event {
        QString job;
        bool done = false;
        bool ok = false;
        int exit_code = 0;
        QString message;
    };
    // An event arrived (on the GUI thread). Before the job's id is known
    // it is kept; once it is, it is acted on if it is this job's.
    void on_event(const Event& event);
    // The agent said which job is ours: the events that beat the reply
    // here are replayed.
    void on_job_id(const QString& id);

Q_SIGNALS:
    void progress(const QString& message);
    // ``restart_needed`` when the installer asked for a restart before
    // Windows starts the driver.
    void succeeded(bool restart_needed);
    void failed(const QString& reason);

private:
    void finish(bool ok, int exit_code, const QString& message);

    void handle(const Event& event);

    link::AgentRpcClient& rpc_;
    QString job_;
    // Between run_script going out and its reply: the job has no id yet,
    // and a script that ends fast reports its end before the reply that
    // names it arrives. Those events wait here.
    bool awaiting_id_ = false;
    QVector<Event> early_;
    link::EventSubscription progress_sub_;
    link::EventSubscription done_sub_;
};

}  // namespace lsw::host::manager
