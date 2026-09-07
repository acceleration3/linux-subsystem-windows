#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QString>
#include <QVector>

#include <cstdint>

namespace lsw::host::provision::debloat {

// One value to write into an offline hive.
struct RegistryTweak {
    // Which hive file, by the name it has in the image's config
    // directory: SOFTWARE, SYSTEM, DEFAULT, or NTUSER for the default
    // user profile.
    QString hive;
    QString key;    // backslash-separated, relative to the hive root
    QString value;  // value name; empty means the key's default value
    std::uint32_t dword = 0;
};

// Writes the offline registry half of a debloat.
//
// Removing a package takes its files out; most of what makes a fresh
// Windows unpleasant is policy rather than payload, and policy lives in
// the hives. Those are files inside install.wim, so they are extracted,
// edited with hivex, and written back — the same thing DISM-based tools
// do by mounting the image and loading the hives into the host's own
// registry, minus the host registry.
//
// The value lists are the ones tiny11 sets, which is where they were
// checked against. Registry paths and DWORDs are Microsoft's own
// documented policy surface; what is chosen here is which of them a
// single-purpose guest wants.
class RegistryTweaks {
public:
    RegistryTweaks() = delete;

    // Every tweak a level applies, in the order they should be written.
    [[nodiscard]] static QVector<RegistryTweak> for_level(DebloatLevel level);

    // Apply ``tweaks`` to the hive files in ``config_dir`` (an extracted
    // ``/Windows/System32/config``) plus ``ntuser_path`` for the default
    // profile. False on failure, with ``error_out`` naming the hive.
    //
    // A key that does not exist is created. A hive the image does not
    // have is skipped, because editions differ and a missing NTUSER.DAT
    // is not a reason to fail a build.
    [[nodiscard]] static bool apply(const QString& config_dir, const QString& ntuser_path,
                                    const QVector<RegistryTweak>& tweaks, QString* error_out);
};

}  // namespace lsw::host::provision::debloat
