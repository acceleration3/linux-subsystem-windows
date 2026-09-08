#pragma once

#include <QString>

namespace lsw::host::platform {

// Finds the two halves of the window mirror on this machine.
//
// remotewin builds as part of this project but is not linked into it:
// the manager spawns the host half for each launched application, and
// the guest half is bundled onto the payload ISO the way the agent is,
// then started inside the guest beside the application it mirrors.
//
// Both follow the rule the agent's path follows — an environment
// variable wins if it names an existing file, otherwise the build tree
// beside the manager binary — so a developer pointing at one build gets
// it honoured everywhere it is asked for.
class MirrorExePaths {
public:
    MirrorExePaths() = delete;

    // ``$LSW_REMOTEWIN`` or the native build's ``remotewin``. Empty when
    // neither exists, which means an application can be launched and its
    // windows will not appear.
    [[nodiscard]] static QString host();

    // ``$LSW_REMOTEWIN_GUEST`` or the cross build's
    // ``remotewin-guest.exe``.
    [[nodiscard]] static QString guest();
};

}  // namespace lsw::host::platform
