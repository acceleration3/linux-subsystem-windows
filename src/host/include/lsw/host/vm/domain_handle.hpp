#pragma once

#include <QUuid>

extern "C" {
typedef struct _virConnect* virConnectPtr;
typedef struct _virDomain* virDomainPtr;
}

namespace lsw::host::vm {

// RAII wrapper around a ``virDomainPtr``.
class DomainHandle {
public:
    DomainHandle() = default;

    explicit DomainHandle(virDomainPtr d) noexcept : d_(d) {}

    ~DomainHandle() noexcept;

    DomainHandle(const DomainHandle&) = delete;
    DomainHandle& operator=(const DomainHandle&) = delete;

    DomainHandle(DomainHandle&& o) noexcept : d_(o.d_) { o.d_ = nullptr; }

    DomainHandle& operator=(DomainHandle&& o) noexcept;

    // The domain with ``uuid`` on ``conn``, or an invalid handle when
    // there is none or ``conn`` is null. Takes the raw connection rather
    // than a LibvirtConnection so it can run on a worker thread with a
    // pointer captured on the GUI one.
    [[nodiscard]] static DomainHandle lookup(virConnectPtr conn, const QUuid& uuid);

    [[nodiscard]] virDomainPtr raw() const noexcept { return d_; }

    [[nodiscard]] bool valid() const noexcept { return d_ != nullptr; }

private:
    virDomainPtr d_ = nullptr;
};

}  // namespace lsw::host::vm
