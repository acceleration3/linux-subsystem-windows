#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>

class QPainter;

namespace lsw::host::manager {


// Diagnostic overlay for coordinate drift, enabled by ``LSW_DEBUG_NCHIT=1``.
//
// It exists because the failure mode it debugs is invisible otherwise:
// when clicks land next to the buttons instead of on them, the only way
// to attribute the drift is to see the frame rect, the image size, the
// device pixel ratio and the hit result together in one screenshot.
class NcHitDebugOverlay {
public:
    // Read once, so flipping the variable needs a restart but the
    // per-paint cost is a static load.
    [[nodiscard]] static bool enabled();

    // Record the last press and what the guest said about it.
    void record(QPointF widget_position, QPoint guest_position, int hit_code);

    // Draw the button rects, the press marker and the diagnostic text.
    // ``frame_rect`` is where the captured image is drawn.
    void paint(QPainter& painter, QRect frame_rect,
               const QImage& image, int widget_width, int widget_height,
               double device_pixel_ratio) const;

private:
    // < 0 (HTERROR) until the first press.
    int hit_code_ = -1;
    QPointF widget_position_;
    QPoint guest_position_;
};

}  // namespace lsw::host::manager
