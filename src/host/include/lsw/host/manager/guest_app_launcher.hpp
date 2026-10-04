#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::apps { struct AppConfig; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Starts a configured application in the guest and carries its windows
// onto this desktop.
//
// The one launch path. A double-click in the app grid and a launcher
// click routed through GuestAppLaunchController both end here, so both
// start the program with the same arguments and the same environment,
// and both mirror it the same way. There is nothing graphics-specific
// to pass: every process opens the guest's adapters through Windows'
// own loaders, as on a physical card.
class GuestAppLauncher : public QObject {
    Q_OBJECT
public:
    explicit GuestAppLauncher(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    // Launch ``app``: its path, with its arguments and its environment,
    // its windows grouped under its launcher entry.
    // A config with no id has no entry, and its windows group under the
    // manager.
    void launch(const apps::AppConfig& app);

Q_SIGNALS:
    // The agent has started it, and a mirror is carrying its windows.
    void launched(const QString& guest_path, quint32 pid);
    void failed(const QString& guest_path, const QString& error);

private:
    link::AgentRpcClient& rpc_;
};

}  // namespace lsw::host::manager
