#include "lsw/host/apps/windows_badge.hpp"

#include <QBuffer>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace lsw::host::apps {

namespace {
constexpr double kTileFraction = 0.46;
constexpr double kTileRounding = 0.22;
constexpr double kPaneInset = 0.2;
constexpr double kPaneGap = 0.07;

void draw_mark(QPainter& painter, const QRectF& tile) {
    QPainterPath outline;
    const double radius = tile.width() * kTileRounding;
    outline.addRoundedRect(tile.adjusted(-3, -3, 3, 3), radius + 3, radius + 3);
    painter.fillPath(outline, QColor(0, 0, 0, 70));

    QPainterPath body;
    body.addRoundedRect(tile, radius, radius);
    painter.fillPath(body, QColor(0xff, 0xff, 0xff));

    const double inset = tile.width() * kPaneInset;
    const double gap = tile.width() * kPaneGap;
    const double pane = (tile.width() - 2 * inset - gap) / 2;
    QLinearGradient blue(tile.topLeft(), tile.bottomRight());
    blue.setColorAt(0, QColor(0x4c, 0xc2, 0xff));
    blue.setColorAt(1, QColor(0x00, 0x67, 0xc0));
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const QRectF square(tile.left() + inset + column * (pane + gap),
                                tile.top() + inset + row * (pane + gap), pane, pane);
            painter.fillRect(square, blue);
        }
    }
}

}

QByteArray WindowsBadge::applied(const QByteArray& png) {
    QImage icon;
    if (!icon.loadFromData(png)) return {};

    QImage canvas(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage scaled = icon.scaled(kSize, kSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(QPointF((kSize - scaled.width()) / 2.0, (kSize - scaled.height()) / 2.0), scaled);

    const double side = kSize * kTileFraction;
    draw_mark(painter, QRectF(kSize - side - 4, kSize - side - 4, side, side));
    painter.end();

    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    canvas.save(&buffer, "PNG");
    return out;
}

}
