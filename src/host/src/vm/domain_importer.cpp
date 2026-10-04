#include "lsw/host/vm/domain_importer.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <QString>
#include <QUuid>
#include <libvirt/libvirt.h>

namespace lsw::host::vm {

bool DomainImporter::import_existing(LibvirtConnection& conn, const config::Settings& settings,
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
    rec.disk_path = settings.vm_disk_path();
    rec.nvram_path = settings.vm_nvram_path();
    rec.payload_iso_path = settings.vm_payload_iso_path();
    vms.add(std::move(rec));
    LSW_INFO("found existing libvirt domain '{}', registered with manager", config::kVmName);
    return true;
}

}  // namespace lsw::host::vm
