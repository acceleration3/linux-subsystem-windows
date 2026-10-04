#include "lsw/host/ui/field_stack.hpp"

#include "lsw/host/ui/widgets.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace lsw::host::ui {

QVBoxLayout* FieldStack::column(const QString& name, QWidget* control) {
    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);
    column->addWidget(Widgets::caption(name));
    column->addWidget(control);
    return column;
}

void FieldStack::add(const QString& name, QWidget* control) {
    into_->addLayout(column(name, control));
}

void FieldStack::add_note(const QString& text) {
    into_->addWidget(Widgets::meta(text, true));
}

}  // namespace lsw::host::ui
