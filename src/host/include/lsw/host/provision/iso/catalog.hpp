#pragma once

#include <QString>

namespace lsw::host::provision::iso {

// Download locations that are fixed rather than scraped. Everything
// here is a constant of the outside world, not of a session — which is
// why it needs no network round trip and no instance.
class IsoCatalog {
public:
    IsoCatalog() = delete;

    [[nodiscard]] static QString virtio_win_url();

    // Looking Glass host package — the only redistributable source of a
    // signed ivshmem.sys. Fetched on the HOST, before the VM exists,
    // because the device it enables is the transport: a driver delivered
    // over the channel it provides only bootstraps if a second transport
    // is there to carry it.
    [[nodiscard]] static QString looking_glass_host_url();

    // The filename that download must be saved under for
    // ``IvshmemDriver::locate`` to find it.
    [[nodiscard]] static QString looking_glass_host_filename();
};

}  // namespace lsw::host::provision::iso
