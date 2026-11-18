#include "lsw/host/ui/field_stack.hpp"

#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

QVBoxLayout* FieldStack::column(const QString& name, QWidget* control, const QString& about) {
    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);
    auto* label = new QHBoxLayout;
    label->setSpacing(6);
    label->addWidget(Widgets::caption(name));
    label->addWidget(new InfoTip(about));
    label->addStretch(1);
    column->addLayout(label);
    column->addWidget(control);
    return column;
}

void FieldStack::add(const QString& name, QWidget* control, const QString& about) {
    into_->addLayout(column(name, control, about));
}

}
