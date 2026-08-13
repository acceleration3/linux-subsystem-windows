#pragma once

#include <QPointF>
#include <Qt>

namespace lsw::host::manager {

// The invisible resize border around a frameless mirror.
//
// The zone is measured from the WIDGET's edges, not the captured
// image's: once the host window grows past the guest's maximum size the
// image letterboxes inside it, and the black margin must not become one
// giant resize grip. The user grabs the host window's edge.
class ResizeEdgeZones {
public:
    ResizeEdgeZones() = delete;

    // Roughly Win11's invisible border: wide enough to grab reliably,
    // narrow enough not to eat content.
    static constexpr int kBorderPx = 7;

    // Which edges ``position`` sits in, or none. Always none when
    // ``border`` is 0 (non-resizable windows and popups).
    [[nodiscard]] static Qt::Edges at(QPointF position, int width, int height,
                                      int border);

    // Matching resize cursor. ArrowCursor when no edge — callers
    // unsetCursor rather than painting a stale arrow.
    [[nodiscard]] static Qt::CursorShape cursor_for(Qt::Edges edges);
};

}  // namespace lsw::host::manager
