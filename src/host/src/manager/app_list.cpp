#include "lsw/host/manager/app_list.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/mirror/window_mirror.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QAction>
#include <QByteArray>
#include <QListWidgetItem>
#include <QMenu>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

AppListWidget::AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                             QWidget* parent)
    : QWidget(parent), store_(store), rpc_(rpc),
      catalog_(rpc), launcher_(rpc) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QToolBar;
    // Text rather than the platform's icon theme: those icons arrive in
    // whatever colours the user's theme picked, which on a dark panel is
    // a pair of bright stickers.
    auto* refresh_act = toolbar_->addAction(QStringLiteral("\u21bb"));
    refresh_act->setToolTip(QStringLiteral("Refresh"));
    auto* new_act = toolbar_->addAction(QStringLiteral("+"));
    new_act->setToolTip(QStringLiteral("Add an application"));
    root->addWidget(toolbar_);

    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setMovement(QListView::Static);
    list_->setResizeMode(QListView::Adjust);
    list_->setIconSize({64, 64});
    list_->setSpacing(8);
    list_->setUniformItemSizes(true);
    // Every tile is exactly this size and never grows to fit a long
    // name; instead the name word-wraps onto extra lines within the
    // fixed cell.
    list_->setGridSize({96, 104});
    list_->setWordWrap(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(list_, 1);

    connect(&catalog_, &apps::AppCatalog::loaded, this, &AppListWidget::on_loaded);
    connect(&launcher_, &apps::Launcher::launched, this, &AppListWidget::on_launched);
    connect(&launcher_, &apps::Launcher::failed, this, &AppListWidget::on_launch_failed);
    // Use itemDoubleClicked rather than itemActivated — the latter is
    // style-dependent (single-click on some platforms, double on
    // others). Explicit double-click is what the user sees.
    connect(list_, &QListWidget::itemDoubleClicked, this, &AppListWidget::on_item_activated);
    connect(list_, &QListWidget::customContextMenuRequested, this,
            &AppListWidget::on_context_menu);
    connect(refresh_act, &QAction::triggered, this, &AppListWidget::on_refresh_clicked);
    connect(new_act, &QAction::triggered, this, &AppListWidget::on_new_clicked);
    connect(&store_, &apps::AppConfigStore::changed, this, &AppListWidget::on_store_changed);

    rebuild();
}

void AppListWidget::set_target(const QUuid& vm_uuid) {
    uuid_ = vm_uuid;
    rebuild();  // show whatever's already in the store immediately
    if (rpc_.has_target()) {
        catalog_.refresh();  // and kick a fresh enumeration
    }
}

void AppListWidget::clear_target() {
    uuid_ = {};
    rebuild();
}

void AppListWidget::rebuild() {
    list_->clear();
    for (const auto& cfg : store_.configs()) {
        auto* item = new QListWidgetItem(cfg.name, list_);
        QImage img = cfg.icon_image();
        if (!img.isNull()) {
            item->setIcon(QIcon{QPixmap::fromImage(img)});
        }
        item->setData(Qt::UserRole, cfg.id);
        item->setToolTip(cfg.path);
    }
}

void AppListWidget::on_store_changed() {
    rebuild();
    // The store reflects the union of live agent reports and stored
    // user customisations; every change is a potential add/remove
    // that should propagate to the host's XDG menu. ``sync`` is
    // idempotent and cheap (per-entry hash comparison + file write
    // only when contents differ).
    apps::DesktopLauncher::sync(store_.configs());
}

void AppListWidget::on_refresh_clicked() {
    if (rpc_.has_target()) {
        catalog_.refresh();
    }
}

void AppListWidget::on_new_clicked() {
    apps::AppConfig blank;  // empty id -> dialog computes one on save
    open_config_dialog(blank);
}

void AppListWidget::on_loaded(const QList<apps::AppEntry>& entries) {
    store_.merge_from_agent(entries);
    // ``rebuild`` runs via ``on_store_changed`` if the merge actually
    // mutated anything. The desktop-launcher sync, on the other hand,
    // has to run on every refresh — the user may have hand-deleted a
    // .desktop file, or the catalog may have produced identical icon
    // bytes (no store change → no on_store_changed signal) but the
    // on-disk PNGs still need to be rewritten because the agent now
    // produces a different format (e.g. higher-resolution alpha).
    apps::DesktopLauncher::sync(store_.configs());
}

void AppListWidget::on_item_activated(QListWidgetItem* item) {
    if (item == nullptr) {
        const std::string line = "app_list: itemDoubleClicked with null item";
        LSW_DEBUG("{}", line);
        return;
    }
    if (!rpc_.has_target()) {
        const std::string line = "app_list: double-click ignored, no VM target set";
        LSW_WARN("{}", line);
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    const auto* cfg = store_.find(id);
    if (cfg == nullptr || cfg->path.isEmpty()) {
        const std::string line = "app_list: no config for id=" + id.toStdString();
        LSW_WARN("{}", line);
        return;
    }
    const std::string line = "app_list: launching " + cfg->path.toStdString();
    LSW_INFO("{}", line);
    Q_EMIT launch_requested(id);
    launcher_.launch(cfg->path, cfg->cmdline, cfg->env);
}

void AppListWidget::on_launched(const QString& app_id, quint32 pid, const QString& job) {
    const std::string line = "app_list: agent reported launch ok for "
                              + app_id.toStdString() + " (pid=" + std::to_string(pid) + ")";
    LSW_INFO("{}", line);
    if (pid == 0) {
        const std::string msg = "app_list: agent returned pid=0 — old agent without "
                                 "PID-return support? rebuild + reinstall the agent";
        LSW_WARN("{}", msg);
        return;
    }
    if (!rpc_.has_target()) {
        return;
    }
    if (mirrors_.contains(pid)) {
        return;  // PID already mirrored (shouldn't happen — PIDs are unique)
    }
    // The identity this launch's windows present to the task manager:
    // the launcher entry already written for this application, so they
    // group under its own name and icon — and against the launcher the
    // user may have pinned — rather than under the manager.
    QString desktop_id;
    for (const auto& cfg : store_.configs()) {
        if (cfg.path == app_id) {
            desktop_id = apps::DesktopLauncher::entry_id(cfg.id);
            break;
        }
    }
    auto* mirror = new mirror::WindowMirror(rpc_, pid, job, desktop_id, this);
    connect(mirror, &mirror::WindowMirror::finished, this, &AppListWidget::on_mirror_finished);
    mirrors_.insert(pid, mirror);
    if (!mirror->start()) {
        mirrors_.remove(pid);
        mirror->deleteLater();
    }
}

void AppListWidget::on_launch_failed(const QString& app_id, const QString& error) {
    const std::string line = "app_list: launch FAILED for " + app_id.toStdString() + ": "
                              + error.toStdString();
    LSW_WARN("{}", line);
}

void AppListWidget::on_mirror_finished(quint32 pid) {
    auto it = mirrors_.find(pid);
    if (it == mirrors_.end()) {
        return;
    }
    if (auto* mirror = it.value().data(); mirror != nullptr) {
        mirror->deleteLater();
    }
    mirrors_.erase(it);
}

void AppListWidget::on_context_menu(const QPoint& pos) {
    auto* item = list_->itemAt(pos);
    if (item == nullptr) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    auto* open_act = menu.addAction("Open");
    menu.setDefaultAction(open_act);
    menu.addSeparator();
    auto* cfg_act = menu.addAction("Configure...");
    auto* del_act = menu.addAction("Delete");
    auto* chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == open_act) {
        on_item_activated(item);
        return;
    }
    if (chosen == del_act) {
        // Store::remove triggers ``changed`` which calls
        // ``DesktopLauncher::sync(configs)`` from on_store_changed —
        // the now-missing entry naturally gets pruned in that pass.
        store_.remove(id);
        return;
    }
    if (chosen == cfg_act) {
        const auto* cfg = store_.find(id);
        if (cfg != nullptr) {
            open_config_dialog(*cfg);
        }
    }
}

void AppListWidget::open_config_dialog(const apps::AppConfig& initial) {
    AppConfigDialog dlg(initial, rpc_, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    store_.upsert(dlg.result());
}

}  // namespace lsw::host::manager
