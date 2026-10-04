#pragma once

#include <QString>

namespace lsw::host::platform {

// Find something this project builds but does not install: the guest
// agent, the mirror's two halves, accel-virt's QEMU and guest drivers.
//
// One rule for all of them, so a developer learns it once. An
// environment variable wins if it names an existing file — that is how
// a specific build gets pointed at — and otherwise the build tree is
// searched beside the manager binary, which is where scripts/build.sh
// and scripts/build-agent.sh put things.
//
// ``relative_to_exe`` is appended to the directory the manager binary
// is in and cleaned, so it starts with a slash and climbs out with
// "/../..". Empty when neither names an existing file, which every
// caller treats as "not available here" rather than as an error.
[[nodiscard]] QString locate_build_artifact(const QString& env_var, const QString& relative_to_exe);

}  // namespace lsw::host::platform
