#pragma once

class QWidget;

namespace lsw::host::platform {
class HostScale {
public:
    HostScale() = delete;

    [[nodiscard]] static int percent(const QWidget* window);

    [[nodiscard]] static int resolve(int setting, const QWidget* window);
};

}
