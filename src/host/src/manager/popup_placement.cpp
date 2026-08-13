#include "lsw/host/manager/popup_placement.hpp"

#include <cmath>

namespace lsw::host::manager {

QPoint PopupPlacement::anchor(QPoint owner_origin, QRect owner_frame, QSize owner_image,
                              QPoint relative_guest_px) {
    double scale_x = 1.0;
    double scale_y = 1.0;
    if (!owner_image.isEmpty()) {
        if (owner_image.width() > 0 && owner_frame.width() > 0) {
            scale_x = double(owner_frame.width()) / owner_image.width();
        }
        if (owner_image.height() > 0 && owner_frame.height() > 0) {
            scale_y = double(owner_frame.height()) / owner_image.height();
        }
    }
    return owner_origin
           + QPoint(static_cast<int>(std::lround(relative_guest_px.x() * scale_x)),
                    static_cast<int>(std::lround(relative_guest_px.y() * scale_y)));
}

}  // namespace lsw::host::manager
