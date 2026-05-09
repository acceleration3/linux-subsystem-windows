#pragma once

#include <QObject>
#include <QString>

namespace lsw::manager::vm { class AgentRpcClient; }

namespace lsw::manager::apps {

// Tells the guest agent to start an installed app inside the VM. The
// agent uses ``WTSQueryUserToken`` + ``CreateProcessAsUser`` so the
// process runs in the active user's session (so DXVK/VKD3D can find
// the user's GPU/driver state and the LSW ICD points at the host
// render-server).
class Launcher : public QObject {
    Q_OBJECT
public:
    explicit Launcher(vm::AgentRpcClient& rpc, QObject* parent = nullptr);

    // Async — returns immediately and emits launched/failed.
    void launch(const QString& app_id);

Q_SIGNALS:
    // ``pid`` is the spawned process id (0 if the agent couldn't
    // resolve it). Per-launch unique — used by the host to scope a
    // window watcher to this specific instance, so multiple
    // simultaneous launches of the same app each get their own
    // mirror window set.
    void launched(const QString& app_id, quint32 pid);
    void failed(const QString& app_id, const QString& error_message);

private:
    vm::AgentRpcClient& rpc_;
};

}  // namespace lsw::manager::apps
