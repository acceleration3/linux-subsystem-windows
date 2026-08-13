#pragma once

#include "lsw/host/config/vm_sizing.hpp"

#include <QString>
#include <QUuid>

namespace lsw::host::config {

// One VM and the on-disk artifacts that belong to it. Identifiers are
// UUIDs so directories never collide; the user-facing ``name`` stays
// editable.
struct VmRecord {
    QUuid uuid;
    QString name;              // user-facing label, e.g. "Win11-Main"
    QString edition;           // "Win11 Pro", "Win10 LTSC", ...
    quint32 vsock_cid = 0;     // unique per VM, picked by the manager
    QString disk_path;         // qcow2 path under disks/
    QString nvram_path;        // OVMF_VARS path under nvram/
    QString iso_path;          // path of the Windows ISO used at install
    QString payload_iso_path;  // path of the LSW payload ISO
    VmSizing sizing;
    bool first_boot_done = false;  // flipped when the guest agent first pings
};

}  // namespace lsw::host::config
