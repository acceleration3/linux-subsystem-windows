#pragma once

#include <QString>

namespace lsw::host::platform {

// Finds the guest half of updating LSW in Windows on the host:
// lsw-guest-update.ps1, which cmake/LSWGuestUpdate.cmake copies into the
// build tree and installs. It is staged in the guest with the rest of
// the release and run there (docs/guest-update.md).
class GuestUpdateScript {
public:
    GuestUpdateScript() = delete;

    // ``$LSW_GUEST_UPDATE_SCRIPT`` wins if it names an existing file;
    // otherwise the build tree's guest-update/lsw-guest-update.ps1, then
    // the installed share/lsw/guest/lsw-guest-update.ps1, both relative to
    // the manager binary. Empty when none exists.
    [[nodiscard]] static QString locate();
};

}  // namespace lsw::host::platform
