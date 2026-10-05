#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace lsw::host::link { class AgentRpcClient; }

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
    // An application's windows. ``job`` is the Job Object naming the
    // launch, as the ``launch`` reply gave it: it survives a launcher stub
    // that starts the real application and exits, which a process id does
    // not. ``pid`` is the fallback for when the job cannot be opened, and
    // the session the guest half runs in; ``desktop_id`` the launcher
    // entry the mirrored windows present as, so they group under the
    // application's own name rather than the manager's.
    [[nodiscard]] static WindowMirror* of_application(link::AgentRpcClient& rpc, quint32 pid,
                                                      const QString& job,
                                                      const QString& desktop_id,
                                                      QObject* parent = nullptr);

    // The guest's whole primary monitor, as one window titled ``title``
    // (remotewin --desktop), captured inside Windows rather than read off
    // QEMU's display: it needs the agent, and Windows running.
    [[nodiscard]] static WindowMirror* of_desktop(link::AgentRpcClient& rpc, const QString& title,
                                                  QObject* parent = nullptr);

    // Starts the host end, then asks the agent for the guest end. False
    // if the mirror could not be started at all, in which case nothing
    // of it shows.
    bool start();

    // Asks the compositor to bring the mirror's windows to the front.
    void raise() const;

Q_SIGNALS:
    // The mirror ended: every window it had closed, or what it mirrored
    // did.
    void finished();

private:
    // ``what`` names the mirror in the log. ``host_arguments`` follow the
    // port on the host end's command line; ``guest_pid`` (0 for none) and
    // ``desktop`` go to the agent's start_window_mirror.
    WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                 quint32 guest_pid, bool desktop, QObject* parent);

    // A port nothing else is on. Asked for by binding one and letting go
    // of it: the mirror binds it a moment later, and this host is not
    // handing out ports fast enough for anything to slip in between.
    [[nodiscard]] static quint16 free_port();

    void on_host_end_finished(int code, QProcess::ExitStatus status);

    link::AgentRpcClient& rpc_;
    QString what_;
    QStringList host_arguments_;
    quint32 guest_pid_;
    bool desktop_;
    QProcess* host_end_;
};

}  // namespace lsw::host::mirror
