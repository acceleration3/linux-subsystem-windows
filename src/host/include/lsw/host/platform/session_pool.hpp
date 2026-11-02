#pragma once

#include "lsw/host/platform/smb_backend.hpp"
#include "lsw/host/platform/smb_session.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace lsw::host::platform {
class SessionPool {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease();

        [[nodiscard]] bool ok() const noexcept { return pool_ != nullptr; }

        [[nodiscard]] int error() const noexcept { return error_; }

        [[nodiscard]] SmbSession& session() const noexcept;

        [[nodiscard]] std::size_t slot() const noexcept { return slot_; }

        [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

        void discard() noexcept { discard_ = true; }

    private:
        friend class SessionPool;
        Lease(SessionPool* pool, std::size_t slot, std::uint64_t generation);

        explicit Lease(int error) : error_(error) {}

        void release() noexcept;

        SessionPool* pool_ = nullptr;
        std::size_t slot_ = 0;
        std::uint64_t generation_ = 0;
        int error_ = 0;
        bool discard_ = false;
    };

    SessionPool(SmbBackend& backend, std::size_t size);

    [[nodiscard]] Lease acquire();

    [[nodiscard]] Lease try_acquire(const std::vector<std::size_t>& excluded);

    [[nodiscard]] Lease acquire_slot(std::size_t slot, std::uint64_t generation);

    [[nodiscard]] std::size_t connected() const;

private:
    struct Slot {
        std::unique_ptr<SmbSession> session;
        bool busy = false;
        std::uint64_t generation = 1;
    };

    [[nodiscard]] Lease pick(const std::vector<std::size_t>& excluded, bool wait);

    void give_back(std::size_t slot, bool discard) noexcept;

    SmbBackend& backend_;
    mutable std::mutex mutex_;
    std::condition_variable idle_;
    std::vector<Slot> slots_;
};

}  // namespace lsw::host::platform
