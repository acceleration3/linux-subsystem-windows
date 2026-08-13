#include "lsw/host/vm/domain_importer.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <QDir>
#include <QString>
#include <QUuid>
#include <libvirt/libvirt.h>

namespace lsw::host::vm {

bool DomainImporter::import_existing(LibvirtConnection& conn,
                                     const config::Settings& settings,
                                     config::VmState& vms) {
    if (!conn.is_open()) {
        return false;
    }
    auto* d = ::virDomainLookupByName(conn.raw(), config::kVmName);
    if (d == nullptr) {
        return false;  // no VM defined yet — the wizard will create one
    }
    DomainHandle handle{d};

    config::VmRecord rec;
    rec.name = config::kVmName;
    unsigned char uuid_bytes[VIR_UUID_BUFLEN] = {};
    if (::virDomainGetUUID(d, uuid_bytes) == 0) {
        rec.uuid = QUuid::fromRfc4122(
            QByteArray(reinterpret_cast<const char*>(uuid_bytes), VIR_UUID_BUFLEN));
    } else {
        rec.uuid = QUuid::createUuid();  // fallback — non-fatal
    }
    const QString fixed = QString::fromLatin1(config::kVmName);
    rec.disk_path = QDir(settings.disks_dir()).filePath(QStringLiteral("%1.qcow2").arg(fixed));
    rec.nvram_path = QDir(settings.nvram_dir()).filePath(QStringLiteral("%1.fd").arg(fixed));
    rec.payload_iso_path =
        QDir(settings.isos_dir()).filePath(QStringLiteral("%1-payload.iso").arg(fixed));
    vms.add(std::move(rec));
    LSW_INFO("found existing libvirt domain '{}', registered with manager",
             config::kVmName);
    return true;
}

}  // namespace lsw::host::vm
