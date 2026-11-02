#pragma once

#include <QUuid>

extern "C" {
typedef struct _virConnect* virConnectPtr;
typedef struct _virDomain* virDomainPtr;
}

namespace lsw::host::vm {
class DomainHandle {
public:
    DomainHandle() = default;

    explicit DomainHandle(virDomainPtr d) noexcept : d_(d) {}

    ~DomainHandle() noexcept;

    DomainHandle(const DomainHandle&) = delete;
    DomainHandle& operator=(const DomainHandle&) = delete;

    DomainHandle(DomainHandle&& o) noexcept : d_(o.d_) { o.d_ = nullptr; }

    DomainHandle& operator=(DomainHandle&& o) noexcept;

    [[nodiscard]] static DomainHandle lookup(virConnectPtr conn, const QUuid& uuid);

    [[nodiscard]] virDomainPtr raw() const noexcept { return d_; }

    [[nodiscard]] bool valid() const noexcept { return d_ != nullptr; }

private:
    virDomainPtr d_ = nullptr;
};

}
