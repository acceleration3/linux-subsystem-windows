#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {

// Tells the guest agent to start an installed app inside the VM. The
// agent uses ``WTSQueryUserToken`` + ``CreateProcessAsUser`` so the
// process runs in the active user's session — session 0 has no
// desktop, and an application started there runs its init and exits
// before it can show a window.
class Launcher : public QObject {
    Q_OBJECT
public:
    explicit Launcher(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    // Async — returns immediately and emits launched/failed.
    //
    // ``args`` is appended to the app's command line; ``env`` is the
    // app config's environment block — ``NAME=value`` per line —
    // applied to that launch alone.
    void launch(const QString& app_id, const QString& args = {},
                const QString& env = {});

Q_SIGNALS:
    // ``pid`` is the spawned process id (0 if the agent couldn't
    // resolve it). Per-launch unique — used by the host to scope a
    // window watcher to this specific instance, so multiple
    // simultaneous launches of the same app each get their own
    // mirror window set.
    void launched(const QString& app_id, quint32 pid);
    void failed(const QString& app_id, const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
};

}  // namespace lsw::host::apps
