#include "lsw/manager/vm/libvirt_conn.hpp"

#include "lsw/log.hpp"

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

namespace lsw::manager::vm {

namespace {

// Route libvirt's own error reports to LSW_DEBUG instead of letting
// them dump to stderr. Routine "not found" lookups (e.g. checking
// whether the ``lsw`` domain exists at startup) are expected and
// shouldn't produce alarming output. Real failures are still logged
// by the call sites via ``last_error()``.
void lsw_libvirt_error_handler(void* /*userdata*/, virErrorPtr err) {
    if (err == nullptr || err->message == nullptr) {
        return;
    }
    const std::string line = std::string{"libvirt: "} + err->message;
    LSW_DEBUG("{}", line);
}

void install_quiet_error_handler_once() {
    static bool installed = false;
    if (!installed) {
        ::virSetErrorFunc(nullptr, &lsw_libvirt_error_handler);
        installed = true;
    }
}

}  // namespace

LibvirtConnection::LibvirtConnection(QObject* parent) : QObject(parent) {
    install_quiet_error_handler_once();
}

LibvirtConnection::~LibvirtConnection() {
    close();
}

int LibvirtConnection::open() {
    if (conn_ != nullptr) {
        return 0;
    }
    conn_ = ::virConnectOpen("qemu:///session");
    if (conn_ == nullptr) {
        LSW_ERROR("virConnectOpen(qemu:///session) failed: {}", last_error().toStdString());
        return -1;
    }
    LSW_INFO("libvirt connected (qemu:///session)");
    return 0;
}

void LibvirtConnection::close() noexcept {
    if (conn_ != nullptr) {
        ::virConnectClose(conn_);
        conn_ = nullptr;
    }
}

QString LibvirtConnection::last_error() const {
    auto* err = ::virGetLastError();
    if (err == nullptr || err->message == nullptr) {
        return {};
    }
    return QString::fromUtf8(err->message);
}

// ---- DomainHandle --------------------------------------------------------

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

}  // namespace lsw::manager::vm
