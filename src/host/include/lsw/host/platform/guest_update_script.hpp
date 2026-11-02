#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestUpdateScript {
public:
    GuestUpdateScript() = delete;

    [[nodiscard]] static QString locate();
};

}
