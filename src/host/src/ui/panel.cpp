#include "lsw/host/ui/panel.hpp"

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

}  // namespace lsw::host::ui
