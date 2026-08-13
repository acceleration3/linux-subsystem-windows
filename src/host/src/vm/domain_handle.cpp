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

virDomainPtr DomainHandle::release() noexcept {
    auto* d = d_;
    d_ = nullptr;
    return d;
}

}  // namespace lsw::host::vm
