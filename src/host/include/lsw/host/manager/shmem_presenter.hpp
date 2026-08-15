#pragma once

#include "lsw/host/manager/presenter.hpp"

#include <QImage>

namespace lsw::host::manager {

// Presents captured frames: the agent commits BGRA into the IVSHMEM
// frame ring, the manager reads a QImage out of it, this draws it.
//
// This is the behaviour the mirrors have always had, lifted out of
// ``WindowView`` so the widget holds a ``Presenter`` rather than a
// QImage plus the arithmetic that goes with it.
class ShmemPresenter : public Presenter {
public:
    // ``trim_shadow`` is set for popups: WinUI menus arrive with a
    // transparent drop-shadow margin that must be cropped away, or the
    // menu floats in the middle of an oversized window with a second,
    // compositor-drawn shadow around it. Classic Win32 menus have no
    // such margin and the trim comes back empty.
    // ``device_pixel_ratio`` is the widget's. A captured frame is
    // measured in GUEST pixels, and the mirror exists to show them as
    // they are — so they have to land on host pixels one for one,
    // whatever the desktop's scale factor is. Stamping the ratio onto
    // the image is how Qt is told that: it then treats the frame as
    // high-DPI content and stops stretching it to logical size, which
    // on a scaled display made every mirror come out 1.25x or 2x too
    // big and blurred by the upscale.
    void submit(QImage frame, bool trim_shadow, qreal device_pixel_ratio);

    [[nodiscard]] bool has_content() const override { return !image_.isNull(); }
    [[nodiscard]] QSize source_size() const override { return image_.size(); }
    [[nodiscard]] QRect content_rect() const override { return content_rect_; }
    [[nodiscard]] QRect frame_rect(QSize widget_size) const override;
    [[nodiscard]] QPoint map_to_guest(QPointF widget_position,
                                      QSize widget_size) const override;
    void paint(QPainter& painter, QSize widget_size, bool translucent) const override;

    // The image itself, for the paths that still need it: caption-button
    // width tracking and the non-client hit-test debug overlay.
    [[nodiscard]] const QImage& image() const noexcept { return image_; }

    // Size the widget should take to show this frame 1:1 — the trimmed
    // content for a popup, the whole frame otherwise.
    //
    // In LOGICAL units, because that is what QWidget::resize takes: the
    // guest's pixel count divided by the scale factor, so the result
    // occupies exactly that many host pixels.
    [[nodiscard]] QSize natural_size() const;

private:
    QImage image_;
    QRect content_rect_;  // empty = use the whole frame
};

}  // namespace lsw::host::manager
