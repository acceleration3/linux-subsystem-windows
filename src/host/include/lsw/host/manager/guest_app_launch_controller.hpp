#pragma once

#include <QObject>
#include <QString>
#include <QUuid>

namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

// Drives one "open this guest app" request end to end.
//
// The interesting case is a cold VM: the request arrives (from an XDG
// launcher click routed over DBus, or from ``--launch``) while Windows
// is not running. The controller then has to start the domain, show the
// user something while it boots, wait for the agent to become reachable,
// fire the launch RPC, and finally attach an AppWindowWatcher so the
// app's windows surface as host mirrors.
//
// Split out of ``App`` because none of that is composition: it is a
// stateful, multi-step interaction with its own cancellation path.
class GuestAppLaunchController : public QObject {
    Q_OBJECT
public:
    explicit GuestAppLaunchController(services::HostServices& services,
                                      QObject* parent = nullptr);

    // Resolve ``app_id`` to a guest path and launch it, starting the VM
    // first if needed. Repeated calls supersede the pending request.
    void launch(const QString& app_id);

Q_SIGNALS:
    // The agent has started the application. The splash retires on
    // this, rather than on the agent merely being reachable.
    void application_running();
    // The splash's "open the log" button, forwarded to whoever owns a
    // window to show it in.
    void log_requested();

private:
    // Fire the launch RPC now — the agent is known reachable. Spawns the
    // AppWindowWatcher that turns the new PID's windows into mirrors.
    void fire_launch(const QString& guest_path);

    // Cold path: start the domain if it is down, show the boot splash,
    // and arm a one-shot wait on the monitor's agent_reachable flip.
    void start_vm_and_wait(const QUuid& uuid, const QString& app_id,
                           const QString& guest_path);

    // Bind the agent channel to ``uuid`` if the manager window (which
    // normally does this) was never shown for this launch.
    void ensure_channel_target(const QUuid& uuid);

    services::HostServices& services_;

    // The app id whose launch is waiting on the VM to come up. Cleared
    // when it fires, when the user cancels the splash, or when a newer
    // request supersedes it.
    QString pending_app_id_;
};

}  // namespace lsw::host::manager
