#pragma once

#include "lsw/host/link/guest_window_model.hpp"

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/catalog.hpp"
#include "lsw/host/apps/launcher.hpp"

#include <QHash>
#include <QListWidget>
#include <QPointer>
#include <QToolBar>
#include <QUuid>
#include <QWidget>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::manager {

class AppWindowWatcher;

// Toolbar (Refresh + New) above an icon-grid of configured apps.
// Items are loaded from the local ``AppConfigStore``; agent
// discoveries are merged on each refresh so user overrides survive
// re-enumeration. Right-click → Configure / Delete.
//
// Double-click launches the app in the guest and starts an
// ``AppWindowWatcher`` that polls for the app's windows and spawns
// a ``WindowView`` (host-side mirror) for each one as they appear.
class AppListWidget : public QWidget {
    Q_OBJECT
public:
    AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                  link::GuestWindowModel& windows, QWidget* parent = nullptr);

    void set_target(const QUuid& vm_uuid);
    void clear_target();

Q_SIGNALS:
    void launch_requested(const QString& app_id);

private Q_SLOTS:
    void on_loaded(const QList<apps::AppEntry>& entries);
    void on_item_activated(QListWidgetItem* item);
    void on_context_menu(const QPoint& pos);
    void on_refresh_clicked();
    void on_new_clicked();
    void on_store_changed();
    void on_launched(const QString& app_id, quint32 pid);
    void on_launch_failed(const QString& app_id, const QString& error);
    void on_watcher_exhausted(quint32 pid);

private:
    void open_config_dialog(const apps::AppConfig& initial);
    void rebuild();

    apps::AppConfigStore& store_;
    link::AgentRpcClient& rpc_;
    link::GuestWindowModel& windows_;
    apps::AppCatalog catalog_;
    apps::Launcher launcher_;
    QToolBar* toolbar_ = nullptr;
    QListWidget* list_ = nullptr;

    QUuid uuid_;

    // One watcher per launched-instance PID. Each double-click
    // creates a fresh watcher; multiple simultaneous instances of
    // the same app each get their own scoped mirror set.
    QHash<quint32, QPointer<AppWindowWatcher>> watchers_;
};

}  // namespace lsw::host::manager
