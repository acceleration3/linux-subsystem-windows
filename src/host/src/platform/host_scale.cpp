#include "lsw/host/platform/host_scale.hpp"

#include <QWidget>

#include <cmath>

namespace lsw::host::platform {

int HostScale::percent(const QWidget* window) {
    if (window == nullptr) {
        return 100;
    }
    return static_cast<int>(std::lround(window->devicePixelRatioF() * 100.0));
}

int HostScale::resolve(int setting, const QWidget* window) {
    return setting > 0 ? setting : percent(window);
}

}  // namespace lsw::host::platform
