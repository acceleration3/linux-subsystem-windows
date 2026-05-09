#include "lsw/manager/iso/catalog.hpp"

namespace lsw::manager::iso {

QString IsoCatalog::virtio_win_url() {
    // Fedora's "latest" virtio-win redirector.
    return "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/latest-virtio/"
           "virtio-win.iso";
}

}  // namespace lsw::manager::iso
