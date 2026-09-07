#include "lsw/host/manager/popup_placement.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::host::manager {

double PopupPlacement::scale(QRect frame_rect, QRect source_rect) {
    if (source_rect.width() <= 0 || frame_rect.width() <= 0) {
        return 1.0;  // before the first frame; 1:1 is the right guess
    }
    return double(frame_rect.width()) / source_rect.width();
}

QPoint PopupPlacement::to_widget(QPoint guest_px, QRect frame_rect,
                                 QRect source_rect) {
    const double s = scale(frame_rect, source_rect);
    // Origin of the shown sub-rect, then scale, then the frame's own
    // position inside the widget.
    return frame_rect.topLeft()
           + QPoint(static_cast<int>(std::lround((guest_px.x() - source_rect.x()) * s)),
                    static_cast<int>(std::lround((guest_px.y() - source_rect.y()) * s)));
}

QSize PopupPlacement::scaled_size(QSize size, double scale) {
    if (size.isEmpty()) {
        return size;
    }
    return {std::max(1, static_cast<int>(std::lround(size.width() * scale))),
            std::max(1, static_cast<int>(std::lround(size.height() * scale)))};
}

}  // namespace lsw::host::manager
