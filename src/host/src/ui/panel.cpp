#include "lsw/host/ui/panel.hpp"

#include "lsw/host/ui/elided_label.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

Panel Panel::make(const QString& caption) {
    Panel panel;
    panel.frame = new QFrame;
    Theme::surface(panel.frame, Theme::Role::kPanel);

    auto* outer = new QVBoxLayout(panel.frame);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header_widget = new QWidget;
    Theme::surface(header_widget, Theme::Role::kPanelHeader);
    panel.header = new QHBoxLayout(header_widget);
    panel.header->setContentsMargins(12, 8, 12, 8);
    if (!caption.isEmpty()) {
        panel.header->addWidget(Widgets::caption(caption));
    }

    auto* body_widget = new QWidget;
    panel.body = new QVBoxLayout(body_widget);
    panel.body->setContentsMargins(12, 12, 12, 12);
    panel.body->setSpacing(10);

    outer->addWidget(header_widget);
    outer->addWidget(body_widget, 1);
    return panel;
}

Panel Panel::area(const QString& name, const QString& caption, bool danger) {
    Panel panel = make();
    panel.header->setContentsMargins(12, 8, 12, 8);
    panel.header->setSpacing(10);
    auto* title = new ElidedLabel(name);
    title->setObjectName(QString::fromLatin1(Theme::Role::kCardName));
    panel.header->addWidget(title);
    if (!caption.isEmpty()) {
        auto* cap = new ElidedLabel(caption);
        cap->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
        panel.header->addWidget(cap, 1);
    } else {
        panel.header->addStretch(1);
    }
    if (danger) {
        Widgets::set_flag(panel.frame, "danger", true);
        Widgets::set_flag(panel.header->parentWidget(), "danger", true);
        Widgets::set_flag(title, "danger", true);
    }
    return panel;
}

void Panel::add_foot(const QString& meta_text) const {
    auto* foot = new QWidget;
    Theme::surface(foot, Theme::Role::kListFoot);
    auto* line = new QVBoxLayout(foot);
    line->setContentsMargins(12, 9, 12, 9);
    line->addWidget(Widgets::meta(meta_text, true));
    static_cast<QVBoxLayout*>(frame->layout())->addWidget(foot);
}

}
