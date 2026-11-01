#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

class QIcon;

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
// A request may carry documents, which makes it the same flow with more to
// do when it fires: wait for the shared folders to be mounted, copy the
// files that are not in one, find the program for each file that was not
// given one, and open them. The documents ride with the request through
// the boot wait, a retry and the failure splash, so none is lost on the
// way.
//
// Split out of ``App`` because none of that is composition: it is a
// stateful, multi-step interaction with its own cancellation path.
class GuestAppLaunchController : public QObject {
    Q_OBJECT
public:
    // One thing to open.
    struct Request {
        // The configured application to open it with. Empty with documents:
        // Windows' own program for each document's type.
        QString app_id;
        // A program or AppUserModelID already chosen for the documents
        // (what a retry of one group remembers); empty to ask Windows.
        QString target;
        // Windows paths to open.
        QStringList documents;
        // Files on this machine to copy into Windows when the request
        // fires, and open from there.
        QStringList uploads;
        // The guest is being restarted onto a new definition: libvirt's id of
        // the boot that is ending. An agent that answers under that id is the
        // old boot's and does not count; the new boot has another. -1 when
        // nothing is being restarted.
        int restart_from_id = -1;
        // Arguments the program Windows chose has to be given before a
        // document (``-jar`` for a .jar); set with ``target``.
        QString leading_args;
        // The launcher's xdg-activation token, which the application's
        // first window is activated with.
        QString activation_token;

        [[nodiscard]] bool has_documents() const noexcept {
            return !documents.isEmpty() || !uploads.isEmpty();
        }
    };

    explicit GuestAppLaunchController(services::HostServices& services, QObject* parent = nullptr);

    // Resolve ``app_id`` to a guest path and launch it, starting the VM
    // first if needed. Repeated calls supersede the pending request.
    // ``activation_token`` is the launcher's xdg-activation token, which the
    // application's first window is activated with.
    void launch(const QString& app_id, const QString& activation_token);

    // Open documents, starting the VM first if needed. Unlike ``launch``,
    // a request with documents never supersedes another: each file asked
    // for is opened.
    void open(Request request);

    // Say why ``request`` cannot be carried out, on the splash that offers a
    // retry of it.
    void fail(const Request& request, const QString& reason);

Q_SIGNALS:
    // The agent has started the application. The splash retires on
    // this, rather than on the agent merely being reachable.
    void application_running();
    // The splash's "open the log" button, forwarded to whoever owns a
    // window to show it in.
    void log_requested();

private:
    // A request and the program it is for, waiting for the guest.
    struct Waiting {
        Request request;
        // The launch's identity on the splash and in GuestAppLauncher's
        // signals: a program path or AppUserModelID; empty while Windows
        // has yet to say what opens a document.
        QString target;
    };

    // The deferred half of a cold launch, once the agent answers.
    void fire(const Request& request, const QString& target);
    // A plain launch: no documents.
    void launch_application(const Request& request, const QString& target);
    // Documents: the steps that come first, then the launch.
    void fire_documents(const Request& request, const QString& target);
    // The shared folders are mounted by GuestUpkeep after the agent answers:
    // true when documents in them can be opened, else it has arranged to be
    // called again (or has shown why not).
    [[nodiscard]] bool shares_ready_for(const Request& request, const QString& target);
    // Copies ``remaining`` into Windows one at a time, each becoming a
    // document of ``working``; ``original`` is what a failure offers to
    // retry. A copy that fails does not stop the others: ``failed`` (with
    // the files in ``failed_files``) collects those, and the documents that
    // did go are opened.
    void upload_documents(const Request& original, Request working, const QString& target,
                          QStringList remaining, QStringList failed, QStringList failed_files);
    void resolve_handlers(const Request& request, const QString& target,
                          const QStringList& upload_failures, const QStringList& failed_files);
    // One program, with its documents.
    void launch_group(const Request& request, const QString& target);

    // Cold path: start the domain if it is down, show the boot splash,
    // and arm a one-shot wait on the monitor's agent_reachable flip.
    void start_vm_and_wait(const QUuid& uuid, const Waiting& first);

    // Bind the agent channel to ``uuid`` if the manager window (which
    // normally does this) was never shown for this launch.
    void ensure_channel_target(const QUuid& uuid);
    // A failed launch with no splash up: one in its failed state.
    void show_failure(const QString& target, const QString& error, const Request& request);
    // The request that a failed launch of ``path`` with ``documents`` was.
    [[nodiscard]] Request request_for(const QString& path, const QStringList& documents) const;
    // Name and icon the splash shows for a request.
    void describe(const Request& request, const QString& target, QString* name, QIcon* icon) const;

    services::HostServices& services_;
    // The launch itself, shared in kind with the app grid's.
    apps::GuestAppLauncher* launcher_ = nullptr;

    // What waits for the guest to answer, in the order asked. A plain launch
    // replaces the plain ones before it; a document request never replaces
    // anything.
    QVector<Waiting> waiting_;
    // A boot wait is running (its splash and its status connection).
    bool waiting_active_ = false;
    // A restart is being waited out: libvirt's id of the boot that is ending,
    // which the one waited for must differ from; -1 for none.
    int restart_from_id_ = -1;
    // Waits for the shared folders that a Cancel on the splash stops.
    QVector<QPointer<QObject>> upkeep_waiters_;
    QPointer<LaunchSplash> splash_;
    QString splash_path_;
};

}  // namespace lsw::host::manager
