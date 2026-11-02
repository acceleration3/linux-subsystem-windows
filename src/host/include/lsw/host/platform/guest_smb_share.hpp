#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestSmbShare {
public:
    GuestSmbShare() = delete;

    [[nodiscard]] static QString url(const QString& username, const QString& password,
                                     const QString& share);

    [[nodiscard]] static QString signature();

    [[nodiscard]] static QString share_for_scope(const QString& scope);
};

}
