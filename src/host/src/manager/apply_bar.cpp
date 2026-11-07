#include "lsw/host/manager/apply_bar.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace lsw::host::manager {

namespace {
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
}

ApplyBar::ApplyBar(config::PendingChanges& pending, QWidget* parent)
    : QWidget(parent), pending_(pending) {
    Theme::surface(this, Theme::Role::kApplyBar);
    auto* bar = new QHBoxLayout(this);
    bar->setContentsMargins(14, 11, 14, 11);
    bar->setSpacing(14);

    count_ = Widgets::caption(QString{});
    count_->setProperty(Theme::Tone::kProperty, Theme::Tone::kAmber);
    diffs_ = Widgets::meta(QString{}, true);
    diffs_->setTextFormat(Qt::RichText);

    auto* revert = Widgets::button(QStringLiteral("Revert"), Theme::Role::kQuiet);
    auto* apply = Widgets::button(QStringLiteral("Apply and restart the guest"), Theme::Role::kPrimary);

    bar->addWidget(count_);
    bar->addWidget(diffs_, 1);
    bar->addWidget(revert);
    bar->addWidget(apply);

    connect(revert, &QPushButton::clicked, &pending_, &config::PendingChanges::revert);
    connect(apply, &QPushButton::clicked, this, [this] {
        pending_.commit();
        Q_EMIT restart_requested();
    });
    connect(&pending_, &config::PendingChanges::changed, this, &ApplyBar::refresh);
    refresh();
}

NavRail::Page ApplyBar::page_of(const QString& key) {
    if (key == K::kHostGpu || key == K::kRenderGpus || key == K::kHostWindowMib || key == K::kVramReserveMib) {
        return NavRail::Page::Graphics;
    }
    if (key == K::kHostShares) {
        return NavRail::Page::Files;
    }
    return NavRail::Page::Settings;
}

ApplyBar::Counts ApplyBar::counts() const {
    Counts out{};
    for (const QString& key : pending_.keys()) {
        ++out[static_cast<std::size_t>(page_of(key))];
    }
    return out;
}

void ApplyBar::refresh() {
    const int count = pending_.count();
    setVisible(count > 0);
    count_->setText(count == 1 ? QStringLiteral("1 CHANGE NEEDS A RESTART")
                               : QStringLiteral("%1 CHANGES NEED A RESTART").arg(count));
    QStringList parts;
    for (const auto& diff : pending_.diffs()) {
        parts << QStringLiteral("<span style=\"white-space:nowrap\">%1&nbsp;<span style=\"color:%2; "
                                "text-decoration:line-through\">%3</span>&nbsp;"
                                "<span style=\"color:%4\">%5</span></span>")
                     .arg(diff.label.toHtmlEscaped(), QString::fromLatin1(Theme::Color::kFgFaint),
                          diff.from.toHtmlEscaped(), QString::fromLatin1(Theme::Color::kAmber),
                          diff.to.toHtmlEscaped());
    }
    diffs_->setText(parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp; ")));
    Q_EMIT counts_changed();
}

}
