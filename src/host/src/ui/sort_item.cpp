#include "lsw/host/ui/sort_item.hpp"

#include "lsw/host/ui/table_header.hpp"

#include <QHeaderView>
#include <QTableWidget>

namespace lsw::host::ui {

SortItem::SortItem(const QString& text, std::optional<double> value, int natural, const QString& key)
    : QTableWidgetItem(text), value_(value), key_(key.isEmpty() ? text : key), natural_(natural) {
}

SortItem* SortItem::put(QTableWidget* table, int row, int column, const QString& text,
                        std::optional<double> value, const QString& key) {
    auto* item = dynamic_cast<SortItem*>(table->item(row, column));
    if (item == nullptr) {
        item = new SortItem(text, value, row, key);
        table->setItem(row, column, item);
        return item;
    }
    if (item->text() != text) {
        item->setText(text);
    }
    item->value_ = value;
    item->key_ = key.isEmpty() ? text : key;
    item->natural_ = row;
    return item;
}

bool SortItem::operator<(const QTableWidgetItem& other) const {
    const auto* rhs = dynamic_cast<const SortItem*>(&other);
    const QHeaderView* head = tableWidget() != nullptr ? tableWidget()->horizontalHeader() : nullptr;
    if (rhs == nullptr || head == nullptr) {
        return QTableWidgetItem::operator<(other);
    }
    if (TableHeader::natural(head)) {
        return natural_ < rhs->natural_;
    }
    if (value_.has_value() != rhs->value_.has_value()) {
        return TableHeader::order(head) == Qt::DescendingOrder ? !value_.has_value() : value_.has_value();
    }
    if (value_ && rhs->value_ && *value_ != *rhs->value_) {
        return *value_ < *rhs->value_;
    }
    if (!value_) {
        const int compared = key_.compare(rhs->key_, Qt::CaseInsensitive);
        if (compared != 0) {
            return compared < 0;
        }
    }
    return TableHeader::order(head) == Qt::DescendingOrder ? natural_ > rhs->natural_ : natural_ < rhs->natural_;
}

}
