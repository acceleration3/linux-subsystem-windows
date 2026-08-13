#include "lsw/host/manager/geometry_sync.hpp"

#include <algorithm>
#include <utility>

namespace lsw::host::manager {

namespace {
// Long enough that an interactive drag sends one resize at the end
// rather than one per frame.
constexpr int kDebounceMs = 150;
}  // namespace

GeometrySync::GeometrySync(ResizeSender send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    connect(&debounce_, &QTimer::timeout, this, &GeometrySync::flush);
}

void GeometrySync::set_guest_screen(int width, int height) {
    guest_screen_width_ = width;
    guest_screen_height_ = height;
}

bool GeometrySync::widget_resized(QSize widget_size) {
    if (expect_echo_) {
        expect_echo_ = false;
        return false;
    }
    if (!ready_) {
        return false;
    }
    pending_ = widget_size;
    debounce_.start();
    return true;
}

void GeometrySync::flush() {
    int w = pending_.width();
    int h = pending_.height();
    if (guest_screen_width_ > 0) {
        w = std::min(w, guest_screen_width_);
    }
    if (guest_screen_height_ > 0) {
        h = std::min(h, guest_screen_height_);
    }
    if (send_) {
        send_(w, h);
    }
}

}  // namespace lsw::host::manager
