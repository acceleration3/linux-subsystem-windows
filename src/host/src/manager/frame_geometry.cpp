#include "lsw/host/manager/frame_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::host::manager {

QRect FrameGeometry::fit(QSize widget, QSize source) {
    if (source.isEmpty() || widget.isEmpty()) {
        return QRect(QPoint(0, 0), widget);
    }
    const QSize target = source.scaled(widget, Qt::KeepAspectRatio);
    return QRect((widget.width() - target.width()) / 2,
                 (widget.height() - target.height()) / 2, target.width(),
                 target.height());
}

QRect FrameGeometry::opaque_bounds(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    const QImage argb = image.format() == QImage::Format_ARGB32
                            ? image
                            : image.convertToFormat(QImage::Format_ARGB32);
    // The solid menu body; the WinUI shadow ramps far below this.
    constexpr int kSolidAlpha = 160;
    int min_x = argb.width();
    int min_y = argb.height();
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(argb.scanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) < kSolidAlpha) {
                continue;
            }
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
    }
    if (max_x < min_x || max_y < min_y) {
        return {};  // nothing solid — caller uses the full frame
    }
    const QRect bounds(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
    // If "solid" is almost the whole frame there was no shadow margin to
    // trim in the first place.
    if (bounds.width() >= argb.width() - 2 && bounds.height() >= argb.height() - 2) {
        return {};
    }
    return bounds;
}

QPoint FrameGeometry::to_guest(QPointF widget_position, QRect frame_rect, QRect source,
                               QSize image_size) {
    if (frame_rect.width() <= 0 || frame_rect.height() <= 0 || image_size.isEmpty()) {
        return {0, 0};
    }
    const double fx = (widget_position.x() - frame_rect.x()) / double(frame_rect.width());
    const double fy = (widget_position.y() - frame_rect.y()) / double(frame_rect.height());
    const int gx = std::clamp(
        source.x() + static_cast<int>(std::lround(fx * source.width())), 0,
        image_size.width() - 1);
    const int gy = std::clamp(
        source.y() + static_cast<int>(std::lround(fy * source.height())), 0,
        image_size.height() - 1);
    return {gx, gy};
}

}  // namespace lsw::host::manager
