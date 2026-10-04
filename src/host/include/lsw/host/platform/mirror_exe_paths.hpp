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
// beside the manager binary, otherwise the installed tree (host half in
// lib/lsw, guest half in share/lsw/guest) — so a developer pointing at
// one build gets it honoured everywhere it is asked for.
class MirrorExePaths {
public:
    MirrorExePaths() = delete;

    // ``$LSW_REMOTEWIN``, the native build's ``remotewin`` or the
    // installed one. Empty when none exists, which means an application can be launched and its
    // windows will not appear.
    [[nodiscard]] static QString host();

    // ``$LSW_REMOTEWIN_GUEST``, the cross build's
    // ``remotewin-guest.exe`` or the installed one.
    [[nodiscard]] static QString guest();
};

}  // namespace lsw::host::platform
