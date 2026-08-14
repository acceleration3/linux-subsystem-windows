#include "lsw/host/manager/app_list.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/manager/app_window_watcher.hpp"
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
                             link::GuestWindowModel& windows,
                             apps::VulkanCaptureService& vulkan, QWidget* parent)
    : QWidget(parent), store_(store), vulkan_(vulkan), rpc_(rpc), windows_(windows),
      catalog_(rpc), launcher_(rpc) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QToolBar;
    auto* refresh_act =
        toolbar_->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh");
    auto* new_act = toolbar_->addAction(style()->standardIcon(QStyle::SP_FileIcon), "New...");
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

    // A Vulkan-capture app needs two things this one does not: a server
    // on the host replaying its command stream, and an agent that has
    // let go of the region the stream travels through. The first is
    // ours to start; the second the agent does when told the launch is
    // a Vulkan one.
    const bool vulkan = cfg->capture == apps::CaptureMethod::Vulkan;
    if (vulkan && !uuid_.isNull()) {
        // Something has to replay the command stream this application
        // is about to produce, or it blocks on its first Vulkan call.
        // It runs in this process so that the window it opens can send
        // its input back to the guest — which is the only reason the
        // manager hosts it rather than spawning lsw-render-server.
        (void)vulkan_.ensure_running(uuid_);
        vulkan_.stop_forwarding();
    }
    launcher_.launch(cfg->path, vulkan);
}

void AppListWidget::on_launched(const QString& app_id, quint32 pid) {
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
    const auto* launched = store_.find(apps::AppConfig::make_id(app_id));
    if (launched != nullptr && launched->capture == apps::CaptureMethod::Vulkan) {
        // No AppWindowWatcher for a Vulkan-capture launch: its windows
        // would each become a mirror fed by the frame ring, which this
        // application does not use — they would sit there black beside
        // the window that has the picture. What it needs instead is for
        // the replay's window to know which guest window to send input
        // to, which takes the PID's window turning up.
        vulkan_pid_ = pid;
        connect(&windows_, &link::GuestWindowModel::updated, this,
                &AppListWidget::on_windows_for_vulkan, Qt::UniqueConnection);
        windows_.subscribe();
        return;
    }
    if (watchers_.contains(pid)) {
        return;  // PID already tracked (shouldn't happen — PIDs are unique)
    }
    // ``app_id`` is the launched path (the manager launches by
    // cfg->path). Derive the executable basename for the watcher's
    // alias/packaged fallback. Compute it manually — QFileInfo on the
    // Linux host won't split a Windows ``\`` path.
    const qsizetype sep = std::max(app_id.lastIndexOf(QLatin1Char('\\')),
                                   app_id.lastIndexOf(QLatin1Char('/')));
    const QString exe_basename = sep < 0 ? app_id : app_id.mid(sep + 1);
    auto* w = new AppWindowWatcher(rpc_, windows_, pid, exe_basename, this);
    connect(w, &AppWindowWatcher::exhausted, this, &AppListWidget::on_watcher_exhausted);
    watchers_.insert(pid, w);
    w->start();
}

void AppListWidget::on_windows_for_vulkan(const QList<link::GuestWindow>& windows) {
    if (vulkan_pid_ == 0) {
        return;
    }
    for (const auto& w : windows) {
        if (w.pid != vulkan_pid_ || w.hwnd == 0) {
            continue;
        }
        vulkan_.forward_input_to(w.hwnd);
        // One window is all this needs; the enumeration keeps running
        // for every other consumer.
        disconnect(&windows_, &link::GuestWindowModel::updated, this,
                   &AppListWidget::on_windows_for_vulkan);
        windows_.unsubscribe();
        vulkan_pid_ = 0;
        return;
    }
}

void AppListWidget::on_launch_failed(const QString& app_id, const QString& error) {
    const std::string line = "app_list: launch FAILED for " + app_id.toStdString() + ": "
                              + error.toStdString();
    LSW_WARN("{}", line);
}

void AppListWidget::on_watcher_exhausted(quint32 pid) {
    auto it = watchers_.find(pid);
    if (it == watchers_.end()) {
        return;
    }
    if (auto* w = it.value().data(); w != nullptr) {
        w->deleteLater();
    }
    watchers_.erase(it);
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
    const apps::AppConfig out = dlg.result();
    store_.upsert(out);
    // DLL plumbing is a VULKAN-ONLY concern: enabling Vulkan capture
    // hardlinks the DXVK / VKD3D-Proton proxy DLLs next to the exe.
    // WGC captures the window directly and needs no DLLs — and we must
    // NEVER run the "disable" path for it, because that DeleteFile()s a
    // fixed list (d3d8/d3d9/d3d11/dxgi/d3d12/…) by name in the app's
    // own folder and would clobber DLLs the app itself ships. So only
    // push set_capture_method when the method is Vulkan; switching to
    // WGC simply leaves the folder untouched.
    if (out.capture != apps::CaptureMethod::Vulkan) {
        return;
    }
    // Push the (enable) plumbing when Vulkan was just selected — either
    // the method changed to Vulkan, or it's a new config that starts on
    // Vulkan.
    const bool needs_setup =
        out.capture != dlg.initial_capture() || initial.id.isEmpty();
    if (!needs_setup || !rpc_.has_target() || out.path.isEmpty()) {
        return;
    }
    // Fire-and-forget on a worker — RPC takes a moment, no need to
    // block the UI on it. Logged on the agent side; nothing actionable
    // for the user beyond the log.
    const QString path = out.path;
    const QString method = apps::capture_method_to_string(out.capture);
    (void)QtConcurrent::run([this, path, method] {
        nlohmann::json req = {
            {"cmd", linkproto::cmd::kSetCaptureMethod},
            {"params", {{"path", path.toStdString()},
                         {"method", method.toStdString()}}},
        };
        (void)rpc_.call(req);
    });
}

}  // namespace lsw::host::manager
