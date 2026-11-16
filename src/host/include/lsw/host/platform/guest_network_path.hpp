#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestNetworkPath {
public:
    GuestNetworkPath() = delete;

    [[nodiscard]] static QString locate();
};

}
