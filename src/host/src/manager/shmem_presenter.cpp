#include "lsw/host/manager/shmem_presenter.hpp"

#include "lsw/host/manager/frame_geometry.hpp"

#include <QPainter>
#include <QRegion>

namespace lsw::host::manager {

void ShmemPresenter::submit(QImage frame, bool trim_shadow) {
    image_ = std::move(frame);
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
    return content_rect_.isEmpty() ? image_.size() : content_rect_.size();
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

void ShmemPresenter::paint(QPainter& painter, QSize widget_size, bool letterbox) const {
    const QRect widget_rect{QPoint{0, 0}, widget_size};
    // Start from nothing, always. A captured frame carries the window's
    // own alpha — a splash screen shaped by it, a rounded corner, the
    // trimmed margin around a menu — and filling black first is what
    // turned all of those into black boxes. Whatever the frame leaves
    // transparent stays transparent.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(widget_rect, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (image_.isNull()) {
        return;
    }
    const QRect target = frame_rect(widget_size);
    if (letterbox && target != widget_rect) {
        // Black bars where the widget is wider or taller than the frame
        // — which is what a mirror looks like for the instant between
        // the user resizing it and the guest window catching up. Only
        // there: the frame's own area is the frame's business.
        QRegion bars{widget_rect};
        bars -= target;
        for (const QRect& bar : bars) {
            painter.fillRect(bar, Qt::black);
        }
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect source = content_rect_.isEmpty() ? image_.rect() : content_rect_;
    painter.drawImage(target, image_, source);
}

}  // namespace lsw::host::manager
