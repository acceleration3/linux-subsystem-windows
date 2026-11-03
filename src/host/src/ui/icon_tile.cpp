#include "lsw/host/ui/icon_tile.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QPainter>

namespace lsw::host::ui {

namespace {
constexpr int kSide = 30;
constexpr qreal kIconPx = 17.0;
constexpr qreal kRadius = 6.0;
}

IconTile::IconTile(LineIcon::Shape shape, QWidget* parent) : QWidget(parent), shape_(shape) {
    setFixedSize(kSide, kSide);
}

void IconTile::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Theme::color("#1a1f29"));
    painter.setPen(QPen(Theme::color(Theme::Color::kLine), 1.0));
    painter.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), kRadius, kRadius);
    const qreal inset = (kSide - kIconPx) / 2.0;
    LineIcon::paint(painter, shape_, QRectF(inset, inset, kIconPx, kIconPx),
                    Theme::color(Theme::Color::kFgDim));
}

}
