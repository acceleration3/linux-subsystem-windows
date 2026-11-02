#include "lsw/host/vm/domain_handle.hpp"

#include "lsw/base/log.hpp"

#include <libvirt/libvirt.h>

namespace lsw::host::vm {

DomainHandle::~DomainHandle() noexcept {
    if (d_ != nullptr) {
        ::virDomainFree(d_);
    }
}

DomainHandle& DomainHandle::operator=(DomainHandle&& o) noexcept {
    if (this != &o) {
        if (d_ != nullptr) {
            ::virDomainFree(d_);
        }
        d_ = o.d_;
        o.d_ = nullptr;
    }
    return *this;
}

DomainHandle DomainHandle::lookup(virConnectPtr conn, const QUuid& uuid) {
    if (conn == nullptr) {
        return {};
    }
    const QByteArray bytes = uuid.toRfc4122();
    DomainHandle found{
        ::virDomainLookupByUUID(conn, reinterpret_cast<const unsigned char*>(bytes.constData()))};
    if (!found.valid()) {
        LSW_TRACE("domain {} not found", uuid.toString(QUuid::WithoutBraces).toStdString());
    }
    return found;
}

}
