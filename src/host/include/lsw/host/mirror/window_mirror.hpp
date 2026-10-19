#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {

// One launched guest application's windows, or the guest's whole
// desktop, on this desktop.
//
// The mirroring itself is remotewin's, and none of it happens in this
// process: a ``remotewin`` is started for it and owns those windows,
// their input and their cursors, as windows of its own. What is here is
// the pairing — a port, the two ends started in the right order, and the
// process reaped when it ends.
//
// One process per application rather than per window, because a menu has
// to be a popup of the window it was opened from and no compositor lets
// one process place a surface against another's.
//
// The host end inherits the manager's environment whole: it finds the
// compositor by WAYLAND_DISPLAY and places its windows through KWin on
// the session bus (DBUS_SESSION_BUS_ADDRESS), and its log lines go where
// the manager's own do, to the manager's stderr.
class WindowMirror : public QObject {
    Q_OBJECT
public:
    // What a mirror of one launched application is told about it. remotewin
    // is sent every window of the guest's session and shows those that
    // belong to the launch: the launched process's, its program's, and
    // those of any AppUserModelID either turns out to have - how Windows'
    // own taskbar groups windows - with what they own.
    struct Application {
        // The launched process, and the session the guest half runs in.
        quint32 pid = 0;
        // The launched program's guest path, when it is one (an .exe).
        QString image;
        // A Store application's ID, known before any window.
        QString aumid;
        // The launcher entry the mirrored windows present as, so they
        // group under the application's own name rather than the manager's.
        QString desktop_id;
        // Names the guest half's log apart from other mirrors'.
        QString log_name;
    };

    ~WindowMirror() override;
    [[nodiscard]] static WindowMirror* of_application(link::AgentRpcClient& rpc, const Application& application,
                                                      QObject* parent = nullptr);

    // The guest's whole primary monitor, as one window titled ``title``
    // (remotewin --desktop), captured inside Windows rather than read off
    // QEMU's display: it needs the agent, and Windows running.
    [[nodiscard]] static WindowMirror* of_desktop(link::AgentRpcClient& rpc, const QString& title,
                                                  QObject* parent = nullptr);

    // Starts asynchronously. ready means the first window frame arrived;
    // failed names startup and runtime failures.
    bool start();

    // Asks the compositor to bring the mirror's windows to the front.
    void raise() const;

    // The AppUserModelIDs this mirror's windows have, as remotewin found them.
    [[nodiscard]] const QStringList& app_ids() const noexcept { return app_ids_; }

Q_SIGNALS:
    // remotewin found another AppUserModelID among the application's windows.
    void app_identified(const QString& aumid);
    // The mirror ended: every window it had closed, or what it mirrored
    // did.
    void finished();
    void ready();
    void failed(const QString& reason);

private:
    // ``what`` names the mirror in the log. ``host_arguments`` follow the
    // port on the host end's command line; ``guest_pid`` (0 for none),
    // ``desktop`` and ``log_name`` go to the agent's start_window_mirror.
    WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                 quint32 guest_pid, bool desktop, QString log_name, QObject* parent);

    void on_host_end_finished(int code, QProcess::ExitStatus status);
    void read_status();
    void start_guest(quint16 port);
    void fail(const QString& reason);
    void announce_ready();

    link::AgentRpcClient& rpc_;
    QString what_;
    QStringList host_arguments_;
    quint32 guest_pid_;
    bool desktop_;
    QString log_name_;
    QStringList app_ids_;
    QProcess* host_end_;
    QTimer deadline_;
    QByteArray status_buffer_;
    bool started_ = false;
    bool guest_started_ = false;
    bool guest_confirmed_ = false;
    bool first_frame_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

}  // namespace lsw::host::mirror
