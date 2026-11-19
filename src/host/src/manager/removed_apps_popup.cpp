#include "lsw/host/manager/removed_apps_popup.hpp"

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

namespace lsw::host::manager {

using ui::Theme;
using ui::Widgets;

namespace {
constexpr int kIconPx = 20;
constexpr int kWidth = 340;
constexpr int kGap = 4;

}  // namespace

RemovedAppsPopup::RemovedAppsPopup(apps::AppConfigStore& store, QWidget* parent)
    : QFrame(parent, Qt::Popup), store_(store) {
    Theme::surface(this, Theme::Role::kPanel);
    setFixedWidth(kWidth);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(10, 10, 10, 10);
    column->setSpacing(8);

    title_ = Widgets::caption(QString{});
    column->addWidget(title_);

    find_ = new QLineEdit;
    find_->setPlaceholderText(QStringLiteral("Find a removed app"));
    find_->setClearButtonEnabled(true);
    find_->installEventFilter(this);
    column->addWidget(find_);

    list_ = new QListWidget;
    list_->setIconSize({kIconPx, kIconPx});
    list_->setUniformItemSizes(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setMouseTracking(true);
    list_->setCursor(Qt::PointingHandCursor);
    list_->setToolTip(QStringLiteral("Click an app to put it back in the list"));
    column->addWidget(list_);

    none_ = Widgets::meta(QStringLiteral("No removed app matches."));
    column->addWidget(none_);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    all_ = Widgets::button(QString{}, Theme::Role::kQuiet);
    foot->addWidget(all_);
    column->addLayout(foot);

    connect(find_, &QLineEdit::textChanged, this, &RemovedAppsPopup::fill);
    connect(find_, &QLineEdit::returnPressed, this, [this] {
        if (list_->count() > 0) {
            restore_one(list_->item(0)->data(Qt::UserRole).toString());
        }
    });
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { restore_one(item->data(Qt::UserRole).toString()); });
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { restore_one(item->data(Qt::UserRole).toString()); });
    connect(all_, &QPushButton::clicked, this, &RemovedAppsPopup::restore_shown);
}

void RemovedAppsPopup::open_above(QWidget* anchor) {
    find_->clear();
    fill();
    adjustSize();
    const QPoint corner = anchor->mapToGlobal(QPoint(anchor->width(), 0));
    QPoint at(corner.x() - width(), corner.y() - height() - kGap);
    if (const QScreen* screen = anchor->screen(); screen != nullptr) {
        const QRect room = screen->availableGeometry();
        if (at.y() < room.top()) {
            at.setY(anchor->mapToGlobal(QPoint(0, anchor->height())).y() + kGap);
        }
        at.setX(std::clamp(at.x(), room.left(), std::max(room.left(), room.right() - width())));
    }
    move(at);
    show();
    (find_->isVisible() ? static_cast<QWidget*>(find_) : list_)->setFocus();
}

bool RemovedAppsPopup::eventFilter(QObject* watched, QEvent* event) {
    if (watched == find_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Down && list_->count() > 0) {
        list_->setFocus();
        list_->setCurrentRow(0);
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void RemovedAppsPopup::fill() {
    const auto& hidden = store_.hidden();
    const QString wanted = find_->text().trimmed();
    QList<apps::AppConfig> shown;
    for (const auto& cfg : hidden) {
        if (wanted.isEmpty() || cfg.name.contains(wanted, Qt::CaseInsensitive)) {
            shown.push_back(cfg);
        }
    }
    std::sort(shown.begin(), shown.end(), [](const apps::AppConfig& a, const apps::AppConfig& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    title_->setText(hidden.size() == 1 ? QStringLiteral("1 removed app")
                                       : QStringLiteral("%1 removed apps").arg(hidden.size()));
    find_->setVisible(hidden.size() >= kFindFrom || !wanted.isEmpty());

    list_->clear();
    for (const auto& cfg : shown) {
        const QImage image = cfg.icon_image();
        auto* item = new QListWidgetItem(
            image.isNull() ? QIcon{} : QIcon{QPixmap::fromImage(image)}, cfg.name, list_);
        item->setData(Qt::UserRole, cfg.id);
        item->setToolTip(cfg.target());
    }
    const int row = list_->sizeHintForRow(0) > 0 ? list_->sizeHintForRow(0) : kIconPx + 8;
    const int rows = std::clamp(static_cast<int>(shown.size()), 1, kVisibleRows);
    list_->setFixedHeight(rows * row + 2 * list_->frameWidth());
    list_->setVisible(!shown.isEmpty());
    none_->setVisible(shown.isEmpty());

    const bool filtering = !wanted.isEmpty();
    all_->setText(filtering ? QStringLiteral("Restore the %1 shown").arg(shown.size())
                            : QStringLiteral("Restore all %1").arg(shown.size()));
    all_->setVisible(shown.size() > 1);
    adjustSize();
}

void RemovedAppsPopup::restore_one(const QString& id) {
    store_.restore(id);
    Q_EMIT restored(id);
    if (store_.hidden().isEmpty()) {
        hide();
        return;
    }
    fill();
}

void RemovedAppsPopup::restore_shown() {
    QStringList ids;
    for (int i = 0; i < list_->count(); ++i) {
        ids << list_->item(i)->data(Qt::UserRole).toString();
    }
    store_.restore(ids);
    hide();
}

}  // namespace lsw::host::manager
