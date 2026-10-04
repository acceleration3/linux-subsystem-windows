#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/catalog.hpp"
#include "lsw/host/manager/guest_app_launcher.hpp"

#include <QListWidget>
#include <QToolBar>
#include <QUuid>
#include <QWidget>

namespace lsw::host::config { class ManagerSettings; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Toolbar (Refresh + New) above an icon-grid of configured apps.
// Items are loaded from the local ``AppConfigStore``; agent
// discoveries are merged on each refresh so user overrides survive
// re-enumeration. Right-click → Configure / Delete.
//
// Double-click launches the app in the guest through GuestAppLauncher,
// which starts a ``mirror::WindowMirror`` for it: one mirror process per
// launch, which owns that application's windows on this desktop.
class AppListWidget : public QWidget {
    Q_OBJECT
public:
    AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                  const config::ManagerSettings& settings, QWidget* parent = nullptr);

    // The toolbar the grid builds for itself. The Overview page lifts
    // it into the panel's caption row, so refresh and add sit on the
    // same line as the panel's name rather than in a strip of their own.
    [[nodiscard]] QToolBar* toolbar() const noexcept { return toolbar_; }

    void set_target(const QUuid& vm_uuid);
    void clear_target();

private Q_SLOTS:
    void on_loaded(const QList<apps::AppEntry>& entries);
    void on_item_activated(QListWidgetItem* item);
    void on_context_menu(const QPoint& pos);
    void on_refresh_clicked();
    void on_new_clicked();

private:
    void open_config_dialog(const apps::AppConfig& initial);
    void rebuild();

    apps::AppConfigStore& store_;
    link::AgentRpcClient& rpc_;
    apps::AppCatalog catalog_;
    // Each double-click starts another instance with a mirror of its
    // own, so two instances of one application do not share a set of
    // windows. The mirrors are the launcher's children.
    GuestAppLauncher launcher_;
    QToolBar* toolbar_ = nullptr;
    QListWidget* list_ = nullptr;

    QUuid uuid_;
};

}  // namespace lsw::host::manager
