#pragma once

#include <QString>

namespace lsw::host::vm {

// Where the GPU device's export socket is: the unix socket QEMU listens on
// (virtio-gpu-av-pci's ``export-socket=``, accel-virt's
// docs/external-export.md), through which a mirror is handed the guest's
// frames as the host GPU memory they are already in, rather than as pixels
// over the guest's network.
//
// One place says it, because two must agree on it: the domain definition
// QEMU is started from, and every remotewin the manager starts.
//
// Directly in $XDG_RUNTIME_DIR, the directory every session process of the
// user's has, QEMU under qemu:///session among them, and which only the
// user can enter. A directory of its own there would have to exist before
// QEMU starts, and a VM started by anything but the manager - virsh, a
// login after a reboot emptied /run/user - would then fail to start at
// all: QEMU binds the socket, and does not make directories.
class ExportSocket {
public:
    ExportSocket() = delete;

    // $XDG_RUNTIME_DIR/<domain>-export.sock; empty without XDG_RUNTIME_DIR,
    // and then the device has no socket and mirrors take frames as pixels.
    [[nodiscard]] static QString path();
};

}  // namespace lsw::host::vm
