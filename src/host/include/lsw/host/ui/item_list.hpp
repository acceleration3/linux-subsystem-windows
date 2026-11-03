#pragma once

#include <QWidget>

class QVBoxLayout;

namespace lsw::host::ui {
class ItemRow;

class ItemList : public QWidget {
    Q_OBJECT
public:
    explicit ItemList(QWidget* parent = nullptr);

    void clear();
    void add(ItemRow* row);

    [[nodiscard]] int count() const noexcept { return count_; }

private:
    QVBoxLayout* column_ = nullptr;
    ItemRow* last_ = nullptr;
    int count_ = 0;
};

}
