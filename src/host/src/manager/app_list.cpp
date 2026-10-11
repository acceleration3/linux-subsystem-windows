#include "lsw/host/manager/app_list.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/manager/store_app_dialog.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QAction>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

namespace lsw::host::manager {

namespace {
// What the manager has opened of an application, in a tile's
// kLaunchRole: nothing, being opened, or mirrored here.
enum class LaunchMark : int { None, Opening, Running };
constexpr int kLaunchRole = Qt::UserRole + 1;
// The mark sits on the icon's bottom-right corner, ringed in the grid's
// own ground so it reads as cut out of the icon.
constexpr qreal kMarkPx = 15.0;
constexpr qreal kMarkRing = 2.0;
constexpr qreal kMarkOverhang = 4.0;
// A store application's icon kept for the launcher, which HiDPI menus
// draw large; the list's own is the size of its rows.
constexpr int kStoreIconPx = 256;

// A tile as Qt draws it, and its launch mark over the icon's corner.
class AppTileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(p, option, index);
        const auto mark = static_cast<LaunchMark>(index.data(kLaunchRole).toInt());
        if (mark == LaunchMark::None) return;
        QStyleOptionViewItem tile = option;
        initStyleOption(&tile, index);
        const QStyle* style = option.widget != nullptr ? option.widget->style() : nullptr;
        if (style == nullptr) return;
        const QRect icon = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &tile, option.widget);
        const QRectF dot(icon.right() + kMarkOverhang - kMarkPx, icon.bottom() + kMarkOverhang - kMarkPx,
                         kMarkPx, kMarkPx);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(ui::Theme::color(ui::Theme::Color::kSurface));
        p->drawEllipse(dot);
        const QRectF inner = dot.adjusted(kMarkRing, kMarkRing, -kMarkRing, -kMarkRing);
        if (mark == LaunchMark::Running) {
            p->setBrush(ui::Theme::color(ui::Theme::Color::kOk));
            p->drawEllipse(inner);
        } else {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(ui::Theme::color(ui::Theme::Color::kAmber), kMarkRing));
            p->drawEllipse(inner.adjusted(kMarkRing / 2, kMarkRing / 2, -kMarkRing / 2, -kMarkRing / 2));
        }
        p->restore();
    }
};
}  // namespace

AppListWidget::AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                             apps::GuestAppLauncher& launcher, QWidget* parent)
    : QWidget(parent), store_(store), rpc_(rpc), catalog_(rpc), launcher_(launcher) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QToolBar;
    // Text rather than the platform's icon theme: those icons arrive in
    // whatever colours the user's theme picked, which on a dark panel is
    // a pair of bright stickers.
    auto* refresh_act = toolbar_->addAction(QStringLiteral("\u21bb"));
    refresh_act->setToolTip(QStringLiteral("Refresh"));
    // Two kinds of application, added two ways: a program by its path,
    // and a store application by its ID from Windows' own list.
    auto* add_menu = new QMenu(this);
    auto* regular_act = add_menu->addAction(QStringLiteral("Add a regular app"));
    regular_act->setToolTip(QStringLiteral("A program or shortcut, by its path in Windows"));
    auto* store_act = add_menu->addAction(QStringLiteral("Add a store app"));
    store_act->setToolTip(QStringLiteral("One installed from the Microsoft Store"));
    add_menu->setToolTipsVisible(true);
    auto* add_button = new QToolButton;
    add_button->setText(QStringLiteral("+"));
    add_button->setToolTip(QStringLiteral("Add an application"));
    add_button->setMenu(add_menu);
    add_button->setPopupMode(QToolButton::InstantPopup);
    toolbar_->addWidget(add_button);
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
    list_->setItemDelegate(new AppTileDelegate(list_));
    root->addWidget(list_, 1);
    connect(&launcher_, &apps::GuestAppLauncher::changed, this,
            &AppListWidget::update_launch_state);

    connect(&catalog_, &apps::AppCatalog::loaded, this, &AppListWidget::on_loaded);
    // Use itemDoubleClicked rather than itemActivated — the latter is
    // style-dependent (single-click on some platforms, double on
    // others). Explicit double-click is what the user sees.
    connect(list_, &QListWidget::itemDoubleClicked, this, &AppListWidget::on_item_activated);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* open = new QShortcut(QKeySequence(key), list_);
        open->setContext(Qt::WidgetShortcut);
        connect(open, &QShortcut::activated, this,
                [this] { on_item_activated(list_->currentItem()); });
    }
    connect(list_, &QListWidget::customContextMenuRequested, this, &AppListWidget::on_context_menu);
    connect(refresh_act, &QAction::triggered, this, &AppListWidget::on_refresh_clicked);
    connect(regular_act, &QAction::triggered, this, &AppListWidget::on_new_clicked);
    connect(store_act, &QAction::triggered, this, &AppListWidget::add_store_app);
    // The XDG menu follows the store too, but App owns that: it reapplies
    // the integrations on every change, whether or not this grid exists.
    connect(&store_, &apps::AppConfigStore::changed, this, &AppListWidget::rebuild);

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

void AppListWidget::set_agent_online(bool online) {
    agent_online_ = online;
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
        item->setToolTip(cfg.target());
    }
    update_launch_state();
}

void AppListWidget::update_launch_state() {
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        const auto* cfg = store_.find(item->data(Qt::UserRole).toString());
        if (!cfg)
            continue;
        const bool opening = launcher_.opening(*cfg);
        const bool running = launcher_.running(*cfg);
        const LaunchMark mark = opening   ? LaunchMark::Opening
                                : running ? LaunchMark::Running
                                          : LaunchMark::None;
        item->setData(kLaunchRole, static_cast<int>(mark));
        item->setToolTip(cfg->target() + (opening   ? QStringLiteral("\nOpening: waiting for a window")
                                          : running ? QStringLiteral("\nRunning: open it to bring it forward")
                                                    : QString{}));
    }
}

void AppListWidget::on_refresh_clicked() {
    refresh();
}

void AppListWidget::refresh() {
    if (rpc_.has_target()) {
        catalog_.refresh();
    }
}

void AppListWidget::on_new_clicked() {
    apps::AppConfig blank;  // empty id -> dialog computes one on save
    open_config_dialog(blank);
}

void AppListWidget::add_store_app() {
    StoreAppDialog dialog(rpc_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const apps::AppConfig& chosen = dialog.chosen();
    for (const auto& cfg : store_.configs()) {
        if (cfg.aumid.compare(chosen.aumid, Qt::CaseInsensitive) == 0) {
            select(cfg.id);
            return;
        }
    }
    store_.upsert(chosen);
    select(chosen.id);
    // The list's icon is the size of its rows; the launcher wants Windows'
    // largest.
    const QString id = chosen.id;
    rpc_.request_async(linkproto::cmd::kListStoreApps,
                       {{"aumid", chosen.aumid.toStdString()}, {"icon_size", kStoreIconPx}}, this,
                       [this, id](const link::AgentReply& reply) {
        const auto apps = reply.ok ? reply.body.value("apps", nlohmann::json::array())
                                   : nlohmann::json::array();
        const auto icon = apps.empty() ? std::string{}
                                       : apps.front().value("icon_png_base64", std::string{});
        const auto* cfg = store_.find(id);
        if (icon.empty() || cfg == nullptr) {
            LSW_WARN("app_list: no large icon for store app {}: {}", id.toStdString(),
                     reply.ok ? std::string{"Windows has none"} : reply.error);
            return;
        }
        apps::AppConfig updated = *cfg;
        updated.icon_png_base64 = QString::fromStdString(icon);
        store_.upsert(std::move(updated));
    });
}

void AppListWidget::select(const QString& id) {
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->data(Qt::UserRole).toString() == id) {
            list_->setCurrentRow(row);
            list_->scrollToItem(list_->item(row));
            return;
        }
    }
}

void AppListWidget::on_loaded(const QList<apps::AppEntry>& entries) {
    store_.merge_from_agent(entries);
    // ``rebuild`` runs via the store's ``changed`` if the merge actually
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
        LSW_DEBUG("app_list: itemDoubleClicked with null item");
        return;
    }
    if (!rpc_.has_target()) {
        LSW_WARN("app_list: double-click ignored, no VM target set");
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    const auto* cfg = store_.find(id);
    if (cfg == nullptr || cfg->target().isEmpty()) {
        LSW_WARN("app_list: no config for id={}", id.toStdString());
        return;
    }
    // Straight to an agent that would only drop the request, the launch
    // fails at once; it waits for the guest instead.
    if (!agent_online_) {
        Q_EMIT launch_requested(id);
        return;
    }
    launcher_.launch(*cfg);
}

void AppListWidget::on_context_menu(const QPoint& pos) {
    auto* item = list_->itemAt(pos);
    if (item == nullptr) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    const auto* config = store_.find(id);
    auto* open_act =
        menu.addAction(config && launcher_.running(*config) ? "Bring to front" : "Open");
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
        // Store::remove triggers ``changed``, on which App reapplies the
        // integrations — the now-missing entry is pruned in that pass.
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
    if (dlg.removal_requested()) {
        if (!initial.id.isEmpty()) {
            store_.remove(initial.id);
        }
        return;
    }
    // Edited, it is the user's: the agent no longer reporting its program
    // is not a reason to drop it.
    apps::AppConfig edited = dlg.result();
    edited.discovered_via_agent = false;
    store_.upsert(std::move(edited));
}

}  // namespace lsw::host::manager
