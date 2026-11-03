#include "lsw/host/ui/flow_layout.hpp"

#include <QWidget>

#include <algorithm>

namespace lsw::host::ui {

FlowLayout::FlowLayout(QWidget* parent, Mode mode, int horizontal, int vertical)
    : QLayout(parent), mode_(mode), horizontal_(horizontal), vertical_(vertical) {}

FlowLayout::~FlowLayout() {
    qDeleteAll(items_);
}

void FlowLayout::set_cell_width(int width) {
    cell_width_ = width;
    invalidate();
}

void FlowLayout::addItem(QLayoutItem* item) {
    items_.append(item);
}

int FlowLayout::count() const {
    return static_cast<int>(items_.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return items_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index) {
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return arrange(QRect(0, 0, width, 0), false);
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

QSize FlowLayout::sizeHint() const {
    const QMargins margins = contentsMargins();
    int width = 0;
    int height = 0;
    if (mode_ == Mode::Even) {
        int cell = cell_width_;
        for (const QLayoutItem* item : items_) {
            cell = std::max({cell, item->sizeHint().width()});
            height = std::max(height, item->sizeHint().height());
        }
        width = cell * static_cast<int>(items_.size()) +
                horizontal_ * std::max(0, static_cast<int>(items_.size()) - 1);
    } else {
        for (const QLayoutItem* item : items_) {
            width += item->sizeHint().width() + horizontal_;
            height = std::max(height, item->sizeHint().height());
        }
        width = std::max(0, width - horizontal_);
    }
    return {width + margins.left() + margins.right(), height + margins.top() + margins.bottom()};
}

QSize FlowLayout::minimumSize() const {
    const QMargins margins = contentsMargins();
    int width = 0;
    int height = 0;
    for (const QLayoutItem* item : items_) {
        width = std::max(width, item->minimumSize().width());
        height = std::max(height, item->minimumSize().height());
    }
    return {width + margins.left() + margins.right(), height + margins.top() + margins.bottom()};
}

int FlowLayout::arrange(const QRect& rect, bool apply) const {
    const QMargins margins = contentsMargins();
    const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    const int used = mode_ == Mode::Even ? arrange_even(area, apply) : arrange_wrapped(area, apply);
    return used + margins.top() + margins.bottom();
}

int FlowLayout::arrange_wrapped(const QRect& area, bool apply) const {
    int x = area.x();
    int y = area.y();
    int row = 0;
    for (QLayoutItem* item : items_) {
        const int width = std::min(item->sizeHint().width(), std::max(area.width(), 1));
        if (x > area.x() && x + width > area.right() + 1) {
            x = area.x();
            y += row + vertical_;
            row = 0;
        }
        const int height = item->hasHeightForWidth() ? item->heightForWidth(width)
                                                     : item->sizeHint().height();
        if (apply) {
            item->setGeometry(QRect(QPoint(x, y), QSize(width, height)));
        }
        x += width + horizontal_;
        row = std::max(row, height);
    }
    return y + row - area.y();
}

int FlowLayout::arrange_even(const QRect& area, bool apply) const {
    const int n = static_cast<int>(items_.size());
    if (n == 0) {
        return 0;
    }
    int cell = cell_width_;
    int least = 0;
    for (const QLayoutItem* item : items_) {
        cell = std::max(cell, cell_width_ > 0 ? 0 : item->sizeHint().width());
        least = std::max(least, item->minimumSize().width());
    }
    cell = std::max(cell, least);
    int columns = std::clamp((area.width() + horizontal_) / std::max(cell + horizontal_, 1), 1, n);
    while (columns > 1 && n % columns != 0) {
        --columns;
    }
    while (columns > 1 && columns * least + (columns - 1) * horizontal_ > area.width()) {
        --columns;
        while (columns > 1 && n % columns != 0) {
            --columns;
        }
    }
    const int span = area.width() - (columns - 1) * horizontal_;
    const auto left_of = [&](int column) { return area.x() + span * column / columns + column * horizontal_; };
    int y = area.y();
    for (int start = 0; start < n; start += columns) {
        int row = 0;
        for (int i = start; i < std::min(start + columns, n); ++i) {
            const QLayoutItem* item = items_.at(i);
            const int width = left_of(i - start + 1) - left_of(i - start) - horizontal_;
            row = std::max(row, item->hasHeightForWidth() ? item->heightForWidth(width)
                                                          : item->sizeHint().height());
        }
        if (apply) {
            for (int i = start; i < std::min(start + columns, n); ++i) {
                const int column = i - start;
                items_.at(i)->setGeometry(QRect(left_of(column), y,
                                                left_of(column + 1) - left_of(column) - horizontal_,
                                                row));
            }
        }
        y += row + vertical_;
    }
    return y - vertical_ - area.y();
}

}
