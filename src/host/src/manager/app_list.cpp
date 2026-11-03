#include "lsw/host/manager/app_list.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/app_config_dialog.hpp"
#include "lsw/host/manager/store_app_dialog.hpp"
#include "lsw/host/ui/line_icon.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QAction>
#include <QApplication>
#include <QListWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QTextOption>
#include <QToolButton>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

#include <cmath>

namespace lsw::host::manager {

namespace {
enum class LaunchMark : int { None, Opening, Running };
constexpr int kLaunchRole = Qt::UserRole + 1;
constexpr qreal kMarkPx = 15.0;
constexpr qreal kMarkRing = 2.0;
constexpr qreal kMarkOverhang = 4.0;
constexpr int kStoreIconPx = 256;
constexpr int kToolIconPx = 18;

constexpr int kTileWidth = 96;
constexpr int kTilePad = 4;
constexpr int kTileTop = 6;
constexpr int kIconSide = 64;
constexpr int kIconGap = 4;
constexpr int kTileMinHeight = 104;

int lay_out_name(QTextLayout& layout, const QString& name, const QFont& font) {
    QTextOption option;
    option.setAlignment(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setFont(font);
    layout.setText(name);
    layout.setTextOption(option);
    const qreal width = kTileWidth - 2 * kTilePad;
    qreal height = 0;
    layout.beginLayout();
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return static_cast<int>(std::ceil(height));
}

class AppTileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override {
        QTextLayout layout;
        const int text = lay_out_name(layout, index.data(Qt::DisplayRole).toString(), option.font);
        return {kTileWidth, qMax(kTileMinHeight, kTileTop + kIconSide + kIconGap + text + kTilePad)};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem ground = option;
        initStyleOption(&ground, index);
        ground.text.clear();
        ground.icon = QIcon{};
        ground.features &= ~QStyleOptionViewItem::HasDisplay;
        ground.features &= ~QStyleOptionViewItem::HasDecoration;
        const QWidget* widget = option.widget;
        const QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &ground, p, widget);

        const QRect icon(option.rect.left() + (option.rect.width() - kIconSide) / 2,
                         option.rect.top() + kTileTop, kIconSide, kIconSide);
        const QIcon tile_icon = index.data(Qt::DecorationRole).value<QIcon>();
        tile_icon.paint(p, icon, Qt::AlignCenter, (option.state & QStyle::State_Selected) != 0
                                                       ? QIcon::Selected
                                                       : QIcon::Normal);

        QTextLayout layout;
        lay_out_name(layout, index.data(Qt::DisplayRole).toString(), option.font);
        p->save();
        p->setPen(option.palette.color((option.state & QStyle::State_Selected) != 0
                                           ? QPalette::HighlightedText
                                           : QPalette::Text));
        layout.draw(p, QPointF(option.rect.left() + kTilePad, icon.bottom() + 1 + kIconGap));
        p->restore();

        const auto mark = static_cast<LaunchMark>(index.data(kLaunchRole).toInt());
        if (mark == LaunchMark::None) return;
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
}

AppListWidget::AppListWidget(apps::AppConfigStore& store, link::AgentRpcClient& rpc,
                             apps::GuestAppLauncher& launcher, QWidget* parent)
    : QWidget(parent), store_(store), rpc_(rpc), catalog_(rpc), launcher_(launcher) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    toolbar_ = new QToolBar;
    const auto tool_button = [](ui::LineIcon::Shape shape, const QString& tip) {
        auto* button = new QToolButton;
        button->setObjectName(QString::fromLatin1(ui::Theme::Role::kIconTool));
        button->setIcon(ui::LineIcon::icon(shape));
        button->setIconSize({kToolIconPx, kToolIconPx});
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    auto* refresh_button = tool_button(ui::LineIcon::Shape::Refresh, QStringLiteral("Refresh"));
    toolbar_->addWidget(refresh_button);
    auto* add_menu = new QMenu(this);
    auto* regular_act = add_menu->addAction(QStringLiteral("Add a regular app"));
    regular_act->setToolTip(QStringLiteral("A program or shortcut, by its path in Windows"));
    auto* store_act = add_menu->addAction(QStringLiteral("Add a store app"));
    store_act->setToolTip(QStringLiteral("One installed from the Microsoft Store"));
    add_menu->setToolTipsVisible(true);
    auto* add_button =
        tool_button(ui::LineIcon::Shape::Plus, QStringLiteral("Add an application"));
    add_button->setMenu(add_menu);
    add_button->setPopupMode(QToolButton::InstantPopup);
    toolbar_->addWidget(add_button);
    root->addWidget(toolbar_);

    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setMovement(QListView::Static);
    list_->setResizeMode(QListView::Adjust);
    list_->setIconSize({64, 64});
    list_->setSpacing(0);
    list_->setWordWrap(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setItemDelegate(new AppTileDelegate(list_));
    root->addWidget(list_, 1);
    connect(&launcher_, &apps::GuestAppLauncher::changed, this,
            &AppListWidget::update_launch_state);

    connect(&catalog_, &apps::AppCatalog::loaded, this, &AppListWidget::on_loaded);
    connect(&catalog_, &apps::AppCatalog::store_extensions_loaded, &store_,
            &apps::AppConfigStore::set_store_extensions);
    connect(list_, &QListWidget::itemDoubleClicked, this, &AppListWidget::on_item_activated);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* open = new QShortcut(QKeySequence(key), list_);
        open->setContext(Qt::WidgetShortcut);
        connect(open, &QShortcut::activated, this,
                [this] { on_item_activated(list_->currentItem()); });
    }
    connect(list_, &QListWidget::customContextMenuRequested, this, &AppListWidget::on_context_menu);
    connect(refresh_button, &QToolButton::clicked, this, &AppListWidget::on_refresh_clicked);
    connect(regular_act, &QAction::triggered, this, &AppListWidget::on_new_clicked);
    connect(store_act, &QAction::triggered, this, &AppListWidget::add_store_app);
    connect(&store_, &apps::AppConfigStore::changed, this, &AppListWidget::rebuild);

    rebuild();
}

void AppListWidget::set_target(const QUuid& vm_uuid) {
    uuid_ = vm_uuid;
    rebuild();
    if (rpc_.has_target()) {
        catalog_.refresh();
    }
}

void AppListWidget::clear_target() {
    uuid_ = {};
    rebuild();
}

void AppListWidget::set_agent_online(bool online, int boot_id) {
    const bool came_back = online && !agent_online_;
    agent_online_ = online;
    catalog_.set_agent_online(online);
    if (came_back && rpc_.has_target() && (boot_id < 0 || boot_id != refreshed_boot_)) {
        refreshed_boot_ = boot_id;
        catalog_.refresh();
    }
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
        item->setToolTip(cfg.name + QLatin1Char('\n') + cfg.target());
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
        item->setToolTip(cfg->name + QLatin1Char('\n') + cfg->target() + (opening   ? QStringLiteral("\nOpening: waiting for a window")
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
    apps::AppConfig blank;
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
    if (!agent_online_) {
        Q_EMIT launch_requested(id);
        return;
    }
    launcher_.launch(*cfg, QString());
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
    apps::AppConfig edited = dlg.result();
    edited.discovered_via_agent = false;
    store_.upsert(std::move(edited));
}

}
