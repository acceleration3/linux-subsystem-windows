#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUuid>

namespace lsw::host::services {
class HostServices;
}

namespace lsw::host::apps {
class GuestAppLauncher;
}

namespace lsw::host::manager {

class LaunchSplash;

// Drives one "open this guest app" request end to end.
//
// The interesting case is a cold VM: the request arrives (from an XDG
// launcher click routed over DBus, or from ``--launch``) while Windows
// is not running. The controller then has to start the domain, show the
// user something while it boots, wait for the agent to become reachable,
// and finally hand the launch to GuestAppLauncher, which starts the
// application and mirrors its windows.
//
// Split out of ``App`` because none of that is composition: it is a
// stateful, multi-step interaction with its own cancellation path.
class GuestAppLaunchController : public QObject {
    Q_OBJECT
public:
    explicit GuestAppLaunchController(services::HostServices& services, QObject* parent = nullptr);

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
    // The deferred half of a cold launch, once the agent answers.
    void fire_when_ready(const QString& app_id, const QString& guest_path);

    // Cold path: start the domain if it is down, show the boot splash,
    // and arm a one-shot wait on the monitor's agent_reachable flip.
    void start_vm_and_wait(const QUuid& uuid, const QString& app_id, const QString& guest_path);

    // Bind the agent channel to ``uuid`` if the manager window (which
    // normally does this) was never shown for this launch.
    void ensure_channel_target(const QUuid& uuid);

    services::HostServices& services_;
    // The launch itself, shared in kind with the app grid's.
    apps::GuestAppLauncher* launcher_ = nullptr;

    // The app id whose launch is waiting on the VM to come up. Cleared
    // when it fires, when the user cancels the splash, or when a newer
    // request supersedes it.
    QString pending_app_id_;
    QPointer<LaunchSplash> splash_;
    QString splash_path_;
    QSet<QString> requested_paths_;
};

}  // namespace lsw::host::manager
