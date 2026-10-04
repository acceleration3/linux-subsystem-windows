#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::apps { struct AppConfig; }
namespace lsw::host::config { class ManagerSettings; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Starts a configured application in the guest and carries its windows
// onto this desktop.
//
// The one launch path. A double-click in the app grid and a launcher
// click routed through GuestAppLaunchController both end here, so both
// start the program with the same arguments, the same environment and
// the same render-channel pinning, and both mirror it the same way.
// When they were two paths, only one of them knew about channels.
class GuestAppLauncher : public QObject {
    Q_OBJECT
public:
    GuestAppLauncher(link::AgentRpcClient& rpc, const config::ManagerSettings& settings,
                     QObject* parent = nullptr);

    // Launch ``app``: its path, with its arguments, its environment and
    // its render channel, its windows grouped under its launcher entry.
    // A config with no id has no entry, and its windows group under the
    // manager.
    void launch(const apps::AppConfig& app);

Q_SIGNALS:
    // The agent has started it, and a mirror is carrying its windows.
    void launched(const QString& guest_path, quint32 pid);
    void failed(const QString& guest_path, const QString& error);

private:
    // ``env`` with the variables that pin the process to ``channel``
    // appended, or unchanged when the application takes any channel.
    [[nodiscard]] QString with_channel(const QString& env, int channel) const;

    link::AgentRpcClient& rpc_;
    const config::ManagerSettings& settings_;
};

}  // namespace lsw::host::manager
