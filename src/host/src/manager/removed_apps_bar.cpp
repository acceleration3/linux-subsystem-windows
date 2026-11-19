#include "lsw/host/manager/removed_apps_bar.hpp"

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/manager/removed_apps_popup.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
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
    line->addWidget(restore_);
    popup_ = new RemovedAppsPopup(store_, this);
    connect(popup_, &RemovedAppsPopup::restored, this, &RemovedAppsBar::restored);
    connect(restore_, &QPushButton::clicked, this, [this] { popup_->open_above(restore_); });

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

}  // namespace lsw::host::manager
