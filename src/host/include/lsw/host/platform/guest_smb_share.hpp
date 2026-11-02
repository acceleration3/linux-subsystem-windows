#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestSmbShare {
public:
    GuestSmbShare() = delete;

    [[nodiscard]] static QString endpoint(const QString& share);

    [[nodiscard]] static QString mount_source(const QString& share);

    [[nodiscard]] static bool is_mount_source(const QString& source);

    [[nodiscard]] static QString mount_point();

    [[nodiscard]] static QString signature();

    [[nodiscard]] static QString share_for_scope(const QString& scope);
};

}
