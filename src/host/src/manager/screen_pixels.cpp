#include "lsw/host/manager/screen_pixels.hpp"

#include "lsw/host/services/status_hub.hpp"

#include <QScreen>
#include <QWindow>
#include <cmath>

namespace lsw::host::manager {

std::optional<QSize> ScreenPixels::of(const QScreen* screen, const QWindow* window) const {
    if (screen == nullptr) {
        return std::nullopt;
    }
    for (const auto& monitor : status_.graphics().host_monitors) {
        if (monitor.name == screen->name() && monitor.mode.isValid()) {
            return monitor.mode;
        }
    }
    if (window != nullptr && window->screen() == screen) {
        const qreal ratio = window->devicePixelRatio();
        const QSize logical = screen->geometry().size();
        return QSize(static_cast<int>(std::lround(logical.width() * ratio)),
                     static_cast<int>(std::lround(logical.height() * ratio)));
    }
    return std::nullopt;
}

}
