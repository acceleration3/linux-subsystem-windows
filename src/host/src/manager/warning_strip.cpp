#include "lsw/host/manager/warning_strip.hpp"

#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace lsw::host::manager {

using ui::Theme;
using ui::Widgets;

WarningStrip::WarningStrip(QWidget* parent) : QWidget(parent) {
    Theme::surface(this, Theme::Role::kWarnStrip);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 9, 14, 9);
    row->setSpacing(12);
    dot_ = Widgets::meta(QStringLiteral("●"));
    text_ = new QLabel;
    text_->setObjectName(QString::fromLatin1(Theme::Role::kWarnText));
    text_->setWordWrap(true);
    text_->setTextFormat(Qt::RichText);
    buttons_ = new QHBoxLayout;
    buttons_->setSpacing(8);
    row->addWidget(dot_, 0, Qt::AlignVCenter);
    row->addWidget(text_, 1);
    row->addLayout(buttons_);
}

void WarningStrip::set_message(bool critical, const QString& headline, const QString& detail) {
    Widgets::set_role(this, critical ? Theme::Role::kWarnStripCritical : Theme::Role::kWarnStrip);
    Widgets::set_role(text_, critical ? Theme::Role::kWarnTextCritical : Theme::Role::kWarnText);
    Widgets::set_flag(dot_, Theme::Tone::kProperty,
                      QVariant(critical ? Theme::Tone::kBad : Theme::Tone::kAmber));
    // The headline in the plain foreground, the rest in the strip's tint.
    text_->setText(QStringLiteral("<span style=\"color:%1;font-weight:500\">%2</span> %3")
                       .arg(QString::fromLatin1(Theme::Color::kFg), headline.toHtmlEscaped(),
                            detail.toHtmlEscaped()));
}

QPushButton* WarningStrip::add_button(const QString& text, const char* role) {
    auto* button = Widgets::button(text, role);
    buttons_->addWidget(button);
    return button;
}

void WarningStrip::add_dismiss() {
    connect(add_button(QStringLiteral("Dismiss"), Theme::Role::kQuiet), &QPushButton::clicked, this,
            &WarningStrip::dismissed);
}

void WarningStrip::clear_buttons() {
    while (auto* item = buttons_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            // Out of the layout it keeps its last geometry until the
            // deferred delete, painted over whatever moves in.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

}  // namespace lsw::host::manager
