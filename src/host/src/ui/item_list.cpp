#include "lsw/host/ui/item_list.hpp"

#include "lsw/host/ui/item_row.hpp"

#include <QLayoutItem>
#include <QVBoxLayout>

namespace lsw::host::ui {

ItemList::ItemList(QWidget* parent) : QWidget(parent) {
    column_ = new QVBoxLayout(this);
    column_->setContentsMargins(0, 0, 0, 0);
    column_->setSpacing(0);
}

void ItemList::clear() {
    while (QLayoutItem* item = column_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    last_ = nullptr;
    count_ = 0;
}

void ItemList::add(ItemRow* row) {
    if (last_ != nullptr) {
        last_->set_last(false);
    }
    row->set_last(true);
    last_ = row;
    column_->addWidget(row);
    ++count_;
}

}
