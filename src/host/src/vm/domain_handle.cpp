#include "lsw/host/vm/domain_handle.hpp"

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
    // libvirt wants the 16 raw bytes, which is what RFC 4122 order is.
    const QByteArray bytes = uuid.toRfc4122();
    return DomainHandle{
        ::virDomainLookupByUUID(conn, reinterpret_cast<const unsigned char*>(bytes.constData()))};
}

}  // namespace lsw::host::vm
