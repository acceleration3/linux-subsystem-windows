#pragma once

#include "lsw/host/config/vm_sizing.hpp"

#include <QString>
#include <QUuid>

namespace lsw::host::config {
struct VmRecord {
    QUuid uuid;
    QString name;
    QString edition;
    quint32 vsock_cid = 0;
    QString disk_path;
    QString nvram_path;
    QString iso_path;
    QString install_media_path;
    QString payload_iso_path;
    VmSizing sizing;
    bool first_boot_done = false;
};

}
