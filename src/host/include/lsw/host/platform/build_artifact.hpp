#pragma once

#include <QString>

namespace lsw::host::platform {

// Find something this project builds and ships beside the manager: the
// guest agent, the mirror's two halves, accel-virt's QEMU and guest
// drivers.
//
// One rule for all of them, so a developer learns it once, tried in
// this order:
//
//   1. An environment variable, if it names an existing file — that is
//      how a specific build gets pointed at.
//   2. The build tree, beside the manager binary, which is where
//      scripts/build.sh and scripts/build-agent.sh put things.
//   3. The installed tree, beside the manager binary as well: the
//      binary is <prefix>/bin/lsw-manager and the rest is under
//      <prefix>/lib/lsw and <prefix>/share/lsw (the install rules in
//      the CMake files lay it out).
//
// Both trees are found relative to the executable rather than through a
// prefix compiled in, so a package staged under a DESTDIR, or unpacked
// anywhere, finds its own files and never another install's.
//
// ``build_relative`` and ``installed_relative`` are appended to the
// directory the manager binary is in and cleaned, so each starts with a
// slash and climbs out with "/..". Either may be empty to skip that
// step. Empty when nothing names an existing file, which every caller
// treats as "not available here" rather than as an error.
[[nodiscard]] QString locate_build_artifact(const QString& env_var, const QString& build_relative,
                                            const QString& installed_relative);

}  // namespace lsw::host::platform
