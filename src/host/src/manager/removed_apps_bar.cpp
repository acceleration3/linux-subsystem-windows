#include "lsw/host/manager/removed_apps_bar.hpp"

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <algorithm>

namespace lsw::host::manager {

using ui::Theme;
using ui::Widgets;

RemovedAppsBar::RemovedAppsBar(apps::AppConfigStore& store, QWidget* parent)
    : QWidget(parent), store_(store) {
    Theme::surface(this, Theme::Role::kListFoot);
    auto* line = new QHBoxLayout(this);
    line->setContentsMargins(12, 6, 12, 6);
    line->setSpacing(10);
    text_ = new QLabel;
    line->addWidget(text_, 1);

    undo_ = Widgets::button(QStringLiteral("Undo"), Theme::Role::kQuiet);
    undo_->setToolTip(QStringLiteral("Put it back in the list"));
    line->addWidget(undo_);

    restore_ = Widgets::button(QStringLiteral("Restore"), Theme::Role::kQuiet);
    restore_->setToolTip(QStringLiteral("Bring a removed app back to the list"));
    menu_ = new QMenu(restore_);
    restore_->setMenu(menu_);
    line->addWidget(restore_);

    undo_window_.setSingleShot(true);
    undo_window_.setInterval(kUndoMs);
    connect(&undo_window_, &QTimer::timeout, this, [this] {
        last_.clear();
        refresh();
    });
    connect(undo_, &QPushButton::clicked, this, [this] {
        const QString id = last_;
        last_.clear();
        undo_window_.stop();
        bring_back(id);
    });
    connect(menu_, &QMenu::aboutToShow, this, &RemovedAppsBar::fill_menu);
    connect(&store_, &apps::AppConfigStore::changed, this, &RemovedAppsBar::refresh);
    refresh();
}

void RemovedAppsBar::removed(const QString& id) {
    last_ = id;
    undo_window_.start();
    refresh();
}

void RemovedAppsBar::bring_back(const QString& id) {
    store_.restore(id);
    Q_EMIT restored(id);
}

void RemovedAppsBar::refresh() {
    const auto& hidden = store_.hidden();
    setVisible(!hidden.isEmpty());
    const auto last = std::find_if(hidden.begin(), hidden.end(),
                                   [this](const apps::AppConfig& cfg) { return cfg.id == last_; });
    const bool undoable = !last_.isEmpty() && last != hidden.end();
    if (undoable) {
        text_->setText(QStringLiteral("%1 is removed from the list.").arg(last->name));
    } else {
        last_.clear();
        text_->setText(
            hidden.size() == 1
                ? QStringLiteral("1 app is removed from the list.")
                : QStringLiteral("%1 apps are removed from the list.").arg(hidden.size()));
    }
    undo_->setVisible(undoable);
    restore_->setVisible(!undoable);
}

void RemovedAppsBar::fill_menu() {
    menu_->clear();
    QList<apps::AppConfig> hidden = store_.hidden();
    std::sort(hidden.begin(), hidden.end(), [](const apps::AppConfig& a, const apps::AppConfig& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    for (const auto& cfg : hidden) {
        const QImage image = cfg.icon_image();
        auto* action =
            menu_->addAction(image.isNull() ? QIcon{} : QIcon{QPixmap::fromImage(image)}, cfg.name);
        const QString id = cfg.id;
        connect(action, &QAction::triggered, this, [this, id] { bring_back(id); });
    }
    if (hidden.size() > 1) {
        menu_->addSeparator();
        connect(menu_->addAction(QStringLiteral("Restore all %1").arg(hidden.size())),
                &QAction::triggered, this, [this] { store_.restore_all(); });
    }
}

}  // namespace lsw::host::manager
