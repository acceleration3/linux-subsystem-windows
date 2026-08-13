#pragma once

#include "lsw/protocol/shared_region.hpp"

#include <cstddef>
#include <string>

namespace lsw::protocol {

// A POSIX shared-memory region, mapped by name.
//
// The local half of the transport, and also the HOST half of the VM
// case: the manager already creates exactly this and hands the fd to
// QEMU, which is how the guest ends up looking at the same pages. So
// this class is not a stand-in for the VM path — it is one end of it.
//
// ``create`` truncates and zeroes; ``open`` attaches to what is already
// there. The creator owns the name and unlinks it on destruction.
class PosixShmRegion final : public SharedRegion {
public:
    PosixShmRegion() = default;
    ~PosixShmRegion() override;

    // Create (or replace) ``/name`` of ``size`` bytes and map it. A
    // leftover region from a crashed run is unlinked first — startup
    // that fails because the last run died is not robustness.
    [[nodiscard]] int create(const std::string& name, std::size_t size);

    // Attach to an existing ``/name``. Size comes from the region
    // itself, so a peer cannot disagree about it.
    [[nodiscard]] int open(const std::string& name);

    void close() noexcept;

    [[nodiscard]] void* base() const noexcept override { return base_; }
    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] std::string description() const override { return "shm://" + name_; }

private:
    std::string name_;
    int fd_ = -1;
    void* base_ = nullptr;
    std::size_t size_ = 0;
    bool owns_name_ = false;  // only the creator unlinks
};

}  // namespace lsw::protocol
