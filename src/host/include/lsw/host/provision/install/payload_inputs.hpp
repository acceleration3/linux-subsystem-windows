#pragma once

#include "lsw/host/services/guest_bundle.hpp"

#include <QString>

#include <memory>

namespace lsw::host::provision::install {

// Configures what goes onto the secondary ISO mounted alongside the
// Windows ISO. The autounattend.xml at the root drives the install; the
// rest is LSW's release for the guest, under ``\update``, which SetupComplete
// hands to lsw-guest-update.ps1 (docs/guest-update.md, "First install").
struct PayloadInputs {
    QString output_iso_path;   // where to write the resulting .iso
    QString autounattend_xml;  // already-rendered XML (string)

    // What the manager would ship to a running guest now: the agent, the
    // mirror's guest half, the driver package, the loaders, the shared
    // folders' files, the update script and the names of this host's GPUs.
    // Staged as ``\update`` in the layout the script reads (each file under
    // its guest path with the drive's colon removed, plus manifest.txt and
    // shares.txt), so the files are byte for byte an update's and the
    // guest's release is the bundle's. Whether a guest may be built without
    // it is not the ISO's decision: a null or failed bundle fails the build,
    // because a guest whose only display is a device with no driver has no
    // desktop once the install-time VGA adapter is gone.
    std::shared_ptr<const services::GuestBundle::Contents> bundle;
};

}  // namespace lsw::host::provision::install
