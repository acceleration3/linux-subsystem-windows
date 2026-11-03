#include "lsw/host/manager/nav_icon.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QAbstractButton>
#include <QPainter>

namespace lsw::host::manager {

namespace {
constexpr int kBox = 20;
}

NavIcon::NavIcon(ui::LineIcon::Shape shape, QWidget* parent) : QWidget(parent), shape_(shape) {
    setFixedSize(kBox, kBox);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QColor NavIcon::tone() const {
    const auto* button = qobject_cast<const QAbstractButton*>(parentWidget());
    if (button != nullptr && button->isChecked()) {
        return ui::Theme::color(ui::Theme::Color::kAmber);
    }
    if (button != nullptr && button->underMouse()) {
        return ui::Theme::color(ui::Theme::Color::kFgDim);
    }
    return ui::Theme::color(ui::Theme::Color::kFgFaint);
}

void NavIcon::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    ui::LineIcon::paint(painter, shape_, QRectF(0, 0, width(), height()), tone());
}

}
