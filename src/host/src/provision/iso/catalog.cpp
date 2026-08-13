#include "lsw/host/provision/iso/catalog.hpp"

namespace lsw::host::provision::iso {

QString IsoCatalog::virtio_win_url() {
    // Fedora's "latest" virtio-win redirector.
    return "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/latest-virtio/"
           "virtio-win.iso";
}

QString IsoCatalog::looking_glass_host_url() {
    // The signed ivshmem driver is not on the virtio-win ISO — Red Hat
    // does not ship it — and Looking Glass's host package is the only
    // redistributable place it exists. This redirector always points at
    // the current stable host build.
    return "https://looking-glass.io/artifact/stable/host";
}

QString IsoCatalog::looking_glass_host_filename() {
    // Saved under a name ``IvshmemDriver::locate`` already looks for, so
    // a package downloaded here is found by the same scan that finds one
    // a user dropped in by hand.
    return QStringLiteral("looking-glass-host.zip");
}

}  // namespace lsw::host::provision::iso
