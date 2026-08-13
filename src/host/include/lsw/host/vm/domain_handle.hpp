#pragma once

extern "C" {
typedef struct _virDomain* virDomainPtr;
}

namespace lsw::host::vm {

// RAII wrapper around a ``virDomainPtr``. ``release`` lets us hand
// ownership back to libvirt without freeing.
class DomainHandle {
public:
    DomainHandle() = default;

    explicit DomainHandle(virDomainPtr d) noexcept : d_(d) {}

    ~DomainHandle() noexcept;

    DomainHandle(const DomainHandle&) = delete;
    DomainHandle& operator=(const DomainHandle&) = delete;

    DomainHandle(DomainHandle&& o) noexcept : d_(o.d_) { o.d_ = nullptr; }

    DomainHandle& operator=(DomainHandle&& o) noexcept;

    [[nodiscard]] virDomainPtr raw() const noexcept { return d_; }

    [[nodiscard]] virDomainPtr release() noexcept;

    [[nodiscard]] bool valid() const noexcept { return d_ != nullptr; }

private:
    virDomainPtr d_ = nullptr;
};

}  // namespace lsw::host::vm
