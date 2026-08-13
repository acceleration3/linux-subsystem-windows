#include "lsw/host/manager/caption_button_rects.hpp"

namespace lsw::host::manager {

void CaptionButtonRects::adopt(QRect minimize, QRect maximize, QRect close, QRect help,
                               int efb_width) {
    minimize_ = minimize;
    maximize_ = maximize;
    close_ = close;
    help_ = help;
    anchored_width_ = efb_width;
}

void CaptionButtonRects::track_frame_width(int efb_width) {
    if (anchored_width_ <= 0 || efb_width <= 0 || efb_width == anchored_width_) {
        return;
    }
    // Top-right anchored: only x moves, by the full width delta.
    const int dx = efb_width - anchored_width_;
    for (QRect* rect : {&minimize_, &maximize_, &close_, &help_}) {
        if (!rect->isEmpty()) {
            rect->translate(dx, 0);
        }
    }
    anchored_width_ = efb_width;
}

CaptionButtonRects::Button CaptionButtonRects::hit(QPoint guest_point) const {
    if (minimize_.contains(guest_point)) return Button::Minimize;
    if (maximize_.contains(guest_point)) return Button::Maximize;
    if (close_.contains(guest_point)) return Button::Close;
    if (help_.contains(guest_point)) return Button::Help;
    return Button::None;
}

}  // namespace lsw::host::manager
