#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {
class WindowMirror;
}

namespace lsw::host::apps {

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
    // ``activation_token`` is an xdg-activation token the launch came with
    // - a launcher click's - which its first window, or an already open
    // mirror raised again, is activated with; empty for none.
    //
    // ``documents`` are guest paths the application is to open. A launch
    // with documents always starts a process: it never takes the "raise
    // only" shortcut, and a mirror the application already has is kept,
    // never replaced, because replacing it would close the windows of the
    // document opened before. The new process's windows reach that mirror,
    // which shows those of its program's image and AppUserModelID (an
    // application started from a shortcut rather than an .exe path has no
    // image to match, and relies on the AppUserModelID its windows carry).
    // While a launch of the same application is still pending the documents
    // wait for it to answer, and are sent then.
    void launch(const apps::AppConfig& app, const QString& activation_token,
                const QStringList& documents = {});

    // The host output (Wayland output name) that stands for the guest's
    // monitor, asked for at each launch: the screen the manager's window is
    // on, which is the one the guest's display is sized from.
    void set_output_source(std::function<QString()> source) { output_source_ = std::move(source); }

Q_SIGNALS:
    // The agent has started it, and a mirror is carrying its windows.
    void launched(const QString& guest_path, quint32 pid);
    // ``documents`` are those of the launch that failed (the guest paths it
    // was asked to open, queued ones included), so whoever retries it has
    // exactly them: a launch is not identified by its program, of which
    // there may be several in flight.
    void failed(const QString& guest_path, const QString& error, const QStringList& documents);
    void changed();

public:
    [[nodiscard]] bool opening(const apps::AppConfig& app) const;
    [[nodiscard]] bool running(const apps::AppConfig& app) const;

private:
    [[nodiscard]] QString key(const apps::AppConfig& app) const;
    // ``identity``'s mirror found ``aumid`` among its windows: the
    // application's identity as Windows' taskbar has it. A second mirror
    // of an application another mirror already shows would show the same
    // windows twice; the one there first keeps them.
    void identified(const QString& identity, const QString& aumid, const QString& path);
    void forget(const QString& identity);
    link::AgentRpcClient& rpc_;
    std::function<QString()> output_source_;
    QSet<QString> pending_;
    // Documents that arrived while their application's launch was pending.
    struct Queued {
        apps::AppConfig app;
        QString activation_token;
        QStringList documents;
    };
    // Sent once the pending launch is over, however it ended.
    void flush_queued(const QString& identity);
    QHash<QString, Queued> queued_;
    QHash<QString, QPointer<mirror::WindowMirror>> mirrors_;
    QHash<QString, quint32> pids_;
    // Which identity's mirror shows each AppUserModelID.
    QHash<QString, QString> shown_by_;
};

}  // namespace lsw::host::apps
