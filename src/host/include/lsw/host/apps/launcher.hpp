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
    // ``pid`` is the spawned process id (a reply without one is a
    // failure) and ``job`` the Job Object the agent named the launch
    // with. Both scope a mirror to this instance, so simultaneous
    // launches of the same application do not show each other's
    // windows; the job is the better of the two, because a launcher
    // stub that starts the real application and exits leaves a process
    // id that names nothing while everything it started is still in the
    // job. Empty when the agent could not make one.
    void launched(const QString& app_id, quint32 pid, const QString& job);
    void failed(const QString& app_id, const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
};

}  // namespace lsw::host::apps
