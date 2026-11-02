#pragma once

#include <QString>

namespace lsw::host::platform {

// Finds the guest half of the shared folders on the host: the tree
// cmake/LSWGuestShares.cmake lays out, with virtio-win's viofs driver and
// virtiofs.exe, the WinFsp installer and lsw-shares.ps1.
//
// Its consumer is services::GuestBundle, which ships it into the guest at
// install (on the payload ISO) and at every update.
class GuestVirtiofsPath {
public:
    GuestVirtiofsPath() = delete;

    // ``$LSW_GUEST_VIRTIOFS`` wins if it names an existing directory;
    // otherwise the build tree's guest-virtiofs/, then the installed
    // share/lsw/guest/virtiofs, both relative to the manager binary.
    // Empty when none exists.
    [[nodiscard]] static QString locate();
};

}  // namespace lsw::host::platform
