#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/catalog.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"

#include <QListWidget>
#include <QToolBar>
#include <QUuid>
#include <QTimer>
#include <QWidget>


namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::manager {
class AppListWidget : public QWidget {
    Q_OBJECT
public:
    AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                  apps::GuestAppLauncher& launcher, QWidget* parent = nullptr);

    [[nodiscard]] QToolBar* toolbar() const noexcept { return toolbar_; }

    void set_target(const QUuid& vm_uuid);
    void refresh();
    void clear_target();
    void set_agent_online(bool online, int boot_id = -1);

Q_SIGNALS:
    void launch_requested(const QString& app_id);

private Q_SLOTS:
    void on_loaded(const QList<apps::AppEntry>& entries);
    void on_item_activated(QListWidgetItem* item);
    void on_context_menu(const QPoint& pos);
    void on_refresh_clicked();
    void on_new_clicked();
    void add_store_app();

private:
    void open_config_dialog(const apps::AppConfig& initial);
    void rebuild();
    void select(const QString& id);

    apps::AppConfigStore& store_;
    link::AgentRpcClient& rpc_;
    apps::AppCatalog catalog_;
    int refreshed_boot_ = -2;
    apps::GuestAppLauncher& launcher_;
    void update_launch_state();
    QToolBar* toolbar_ = nullptr;
    QListWidget* list_ = nullptr;

    QUuid uuid_;
    bool agent_online_ = false;
};

}
