#include "lsw/host/manager/nc_hit_debug_overlay.hpp"

#include "lsw/host/manager/non_client_hit.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace lsw::host::manager {

bool NcHitDebugOverlay::enabled() {
    static const bool on = [] {
        const QByteArray value = qgetenv("LSW_DEBUG_NCHIT");
        return value == "1" || value.toLower() == "true";
    }();
    return on;
}

void NcHitDebugOverlay::record(QPointF widget_position, QPoint guest_position,
                               int hit_code) {
    widget_position_ = widget_position;
    guest_position_ = guest_position;
    hit_code_ = hit_code;
}

void NcHitDebugOverlay::paint(QPainter& painter,
                              QRect frame_rect, const QImage& image, int widget_width,
                              int widget_height, double device_pixel_ratio) const {
    if (image.width() <= 0 || image.height() <= 0) {
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    // No button rects to draw any more: the mirror no longer decides
    // which press is a button, so there is nothing here to disagree with
    // what the user sees. What remains is the press marker, which is
    // still the way to see where a click landed in guest pixels.

    if (hit_code_ < 0) {
        return;  // nothing pressed yet
    }
    // The marker sits at the press position in widget logical
    // coordinates; the label dumps every number that feeds the guest
    // mapping so drift is attributable from a screenshot alone.
    painter.setPen(QPen(Qt::yellow, 2));
    painter.setBrush(QColor(255, 0, 0, 220));
    painter.drawEllipse(widget_position_, 6.0, 6.0);

    const QStringList lines = {
        QStringLiteral("NCHIT %1 (%2)  guest=%3,%4")
            .arg(NonClientHit::name(hit_code_)).arg(hit_code_)
            .arg(guest_position_.x()).arg(guest_position_.y()),
        QStringLiteral("click=%1,%2  widget=%3x%4  dpr=%5")
            .arg(QString::number(widget_position_.x(), 'f', 1))
            .arg(QString::number(widget_position_.y(), 'f', 1))
            .arg(widget_width).arg(widget_height)
            .arg(QString::number(device_pixel_ratio, 'f', 2)),
        QStringLiteral("image=%1x%2  frame=%3,%4 %5x%6")
            .arg(image.width()).arg(image.height())
            .arg(frame_rect.x()).arg(frame_rect.y())
            .arg(frame_rect.width()).arg(frame_rect.height()),
    };

    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    constexpr int kPad = 4;
    const int line_height = metrics.lineSpacing();
    int widest = 0;
    for (const QString& line : lines) {
        widest = std::max(widest, metrics.horizontalAdvance(line));
    }
    const QRect background(2, 2, widest + kPad * 2,
                           line_height * static_cast<int>(lines.size()) + kPad * 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 200));
    painter.drawRect(background);
    painter.setPen(Qt::yellow);
    for (int i = 0; i < lines.size(); ++i) {
        painter.drawText(QRect(background.x() + kPad,
                               background.y() + kPad + line_height * i, widest,
                               line_height),
                         Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
}

}  // namespace lsw::host::manager
