#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

namespace lsw::host::manager {

// The maths that relates a mirror widget's pixels to the guest's.
//
// Every click, every paint and every popup placement goes through the
// same aspect-fit rectangle, so it lives in one place: a discrepancy
// between how a frame is drawn and how a click is mapped shows up as
// "the buttons are in the wrong place", which is miserable to chase.
class FrameGeometry {
public:
    FrameGeometry() = delete;

    // Where a ``source``-sized image is drawn inside a ``widget``-sized
    // area: aspect-fit, centred.
    [[nodiscard]] static QRect fit(QSize widget, QSize source);

    // Bounding box of the solidly-opaque content in ``image``, used to
    // trim the transparent margin WinUI bakes into captured popup
    // shadows so the menu fills its window and the only shadow is the
    // compositor's.
    //
    // Empty when there is nothing to trim — classic Win32 menus have no
    // in-window shadow, and a "solid" box that covers nearly the whole
    // frame means the same thing. Callers use the full frame then.
    [[nodiscard]] static QRect opaque_bounds(const QImage& image);

    // Widget-local position -> guest client pixel.
    //
    // ``source`` is the sub-rectangle actually drawn (the trimmed popup
    // content, or the whole image); guest coordinates are in the full
    // frame's space, so the trimmed origin is added back.
    [[nodiscard]] static QPoint to_guest(QPointF widget_position, QRect frame_rect,
                                         QRect source, QSize image_size);
};

}  // namespace lsw::host::manager
