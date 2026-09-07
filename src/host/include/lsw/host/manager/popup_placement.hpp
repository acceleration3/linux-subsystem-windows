#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

namespace lsw::host::manager {

// The mapping between a mirror's guest pixels and its widget pixels.
//
// A mirror is not a 1:1 view of its window. The widget can be any size
// the user drags it to, the frame is aspect-fitted inside it and centred
// — letterboxed — and the frame itself may be showing only a sub-rect of
// what the guest sent. Three things therefore separate a guest pixel
// from a widget pixel: the sub-rect's origin, the scale, and the
// letterbox offset.
//
// Popups are placed through this and nothing else. Every misplaced menu
// so far came from an anchor that used one or two of the three: dropping
// the letterbox offset put menus up and to the left by the margin,
// dropping the scale put them progressively further out the more the
// mirror had been resized, and dropping the sub-rect origin would move
// them by a maximized window's overhang.
class PopupPlacement {
public:
    PopupPlacement() = delete;

    // Widget px per guest px. Both axes share it: the frame is
    // aspect-fitted, so the ratio is the same bar rounding.
    [[nodiscard]] static double scale(QRect frame_rect, QRect source_rect);

    // A guest pixel of this mirror's window → a point in its widget.
    // ``guest_px`` is measured from the frame's top-left, which is what
    // the guest reports for every window: the same origin input
    // injection adds back on the other side.
    [[nodiscard]] static QPoint to_widget(QPoint guest_px, QRect frame_rect,
                                          QRect source_rect);

    // ``size`` in guest px → the same thing in widget px.
    [[nodiscard]] static QSize scaled_size(QSize size, double scale);
};

}  // namespace lsw::host::manager
