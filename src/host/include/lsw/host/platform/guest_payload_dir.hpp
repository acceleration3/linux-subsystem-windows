#pragma once

#include <QString>
#include <QStringView>

namespace lsw::host::platform {
class GuestPayloadDir {
public:
    GuestPayloadDir() = delete;

    [[nodiscard]] static QString locate(QStringView name);
};

}
