#include "lsw/host/manager/shmem_presenter.hpp"

#include "lsw/host/manager/frame_geometry.hpp"

#include <QPainter>

namespace lsw::host::manager {

void ShmemPresenter::submit(QImage frame, bool trim_shadow, qreal device_pixel_ratio) {
    image_ = std::move(frame);
    if (!image_.isNull() && device_pixel_ratio > 0.0) {
        image_.setDevicePixelRatio(device_pixel_ratio);
    }
    if (trim_shadow && !image_.isNull()) {
        content_rect_ = FrameGeometry::opaque_bounds(image_);
    } else {
        content_rect_ = QRect{};
    }
}

QSize ShmemPresenter::natural_size() const {
    if (image_.isNull()) {
        return {};
    }
    const QSize pixels = content_rect_.isEmpty() ? image_.size() : content_rect_.size();
    const qreal dpr = image_.devicePixelRatio();
    if (dpr <= 0.0) {
        return pixels;
    }
    // Logical units: ``resize`` speaks them, and dividing here is what
    // makes the frame cover exactly its own pixel count on screen.
    return QSizeF(pixels.width() / dpr, pixels.height() / dpr).toSize();
}

QRect ShmemPresenter::frame_rect(QSize widget_size) const {
    if (image_.isNull() || widget_size.width() <= 0 || widget_size.height() <= 0) {
        return {QPoint{0, 0}, widget_size};
    }
    return FrameGeometry::fit(widget_size, natural_size());
}

QPoint ShmemPresenter::map_to_guest(QPointF widget_position, QSize widget_size) const {
    if (image_.isNull()) {
        return {0, 0};
    }
    const QRect source = content_rect_.isEmpty() ? image_.rect() : content_rect_;
    return FrameGeometry::to_guest(widget_position, frame_rect(widget_size), source,
                                   image_.size());
}

void ShmemPresenter::paint(QPainter& painter, QSize widget_size, bool translucent) const {
    const QRect widget_rect{QPoint{0, 0}, widget_size};
    if (translucent) {
        // Anything outside the captured menu must not show as a black box.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(widget_rect, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        // Black through any transient aspect mismatch while the guest
        // catches up to a resize.
        painter.fillRect(widget_rect, Qt::black);
    }
    if (image_.isNull()) {
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect source = content_rect_.isEmpty() ? image_.rect() : content_rect_;
    painter.drawImage(frame_rect(widget_size), image_, source);
}

}  // namespace lsw::host::manager
