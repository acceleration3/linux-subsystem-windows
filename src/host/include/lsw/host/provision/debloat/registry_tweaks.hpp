#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QString>
#include <QVector>

#include <cstdint>

namespace lsw::host::provision::debloat {
struct RegistryTweak {
    QString hive;
    QString key;
    QString value;
    std::uint32_t dword = 0;
};

class RegistryTweaks {
public:
    RegistryTweaks() = delete;

    [[nodiscard]] static QVector<RegistryTweak> for_level(DebloatLevel level);

    [[nodiscard]] static bool apply(const QString& config_dir, const QString& ntuser_path,
                                    const QVector<RegistryTweak>& tweaks, QString* error_out);
};

}
