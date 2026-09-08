#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <cstdint>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mirror {

// One launched guest application's windows, on this desktop.
//
// The mirroring itself is remotewin's, and none of it happens in this
// process: a ``remotewin`` is started for the launch and owns that
// application's windows, its input and its cursors, as windows of its
// own. What is here is the pairing — a port, the two ends started in the
// right order, and the process reaped when the application ends.
//
// One process per application rather than per window, because a menu has
// to be a popup of the window it was opened from and no compositor lets
// one process place a surface against another's.
class WindowMirror : public QObject {
    Q_OBJECT
public:
    // ``job`` is the Job Object naming the launch, as the ``launch``
    // reply gave it: it survives a launcher stub that starts the real
    // application and exits, which a process id does not. ``pid`` is the
    // fallback for when the job cannot be opened, and ``desktop_id`` the
    // launcher entry the mirrored windows present as, so they group
    // under the application's own name rather than the manager's.
    WindowMirror(link::AgentRpcClient& rpc, quint32 pid, QString job, QString desktop_id,
                 QObject* parent = nullptr);

    // Starts the host end, then asks the agent for the guest end. False
    // if the mirror could not be started at all, in which case the
    // application runs in the guest with nothing showing it.
    bool start();

Q_SIGNALS:
    // The mirror ended: every window of that application closed, or the
    // application did.
    void finished(quint32 pid);

private:
    // A port nothing else is on. Asked for by binding one and letting go
    // of it: the mirror binds it a moment later, and this host is not
    // handing out ports fast enough for anything to slip in between.
    [[nodiscard]] static quint16 free_port();

    link::AgentRpcClient& rpc_;
    quint32 pid_;
    QString job_;
    QString desktop_id_;
    QProcess* host_end_;
};

}  // namespace lsw::host::mirror
