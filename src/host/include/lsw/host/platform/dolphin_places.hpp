#pragma once

#include <QString>

namespace lsw::host::platform {
class DolphinPlaces {
public:
    [[nodiscard]] static bool register_windows_bookmark(const QString& username,
                                                        const QString& password,
                                                        const QString& share
                                                        = QStringLiteral("C$"));

    static void remove_windows_bookmark();
};

}
