#include "lsw/host/platform/session_pool.hpp"

#include <algorithm>
#include <cerrno>
#include <utility>

namespace lsw::host::platform {

SessionPool::Lease::Lease(SessionPool* pool, std::size_t slot, std::uint64_t generation)
    : pool_(pool), slot_(slot), generation_(generation) {}

SessionPool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)), slot_(other.slot_),
      generation_(other.generation_), error_(other.error_), discard_(other.discard_) {}

SessionPool::Lease& SessionPool::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::exchange(other.pool_, nullptr);
        slot_ = other.slot_;
        generation_ = other.generation_;
        error_ = other.error_;
        discard_ = other.discard_;
    }
    return *this;
}

SessionPool::Lease::~Lease() {
    release();
}

void SessionPool::Lease::release() noexcept {
    if (pool_ != nullptr) {
        pool_->give_back(slot_, discard_);
        pool_ = nullptr;
    }
}

SmbSession& SessionPool::Lease::session() const noexcept {
    return *pool_->slots_[slot_].session;
}

SessionPool::SessionPool(SmbBackend& backend, std::size_t size) : backend_(backend), slots_(size) {}

void SessionPool::give_back(std::size_t slot, bool discard) noexcept {
    std::unique_ptr<SmbSession> dropped;
    {
        const std::lock_guard lock{mutex_};
        Slot& s = slots_[slot];
        if (discard) {
            dropped = std::move(s.session);
            ++s.generation;
        }
        s.busy = false;
    }
    dropped.reset();
    idle_.notify_all();
}

SessionPool::Lease SessionPool::acquire() {
    return pick({}, true);
}

SessionPool::Lease SessionPool::try_acquire(const std::vector<std::size_t>& excluded) {
    return pick(excluded, false);
}

SessionPool::Lease SessionPool::pick(const std::vector<std::size_t>& excluded, bool wait) {
    std::unique_lock lock{mutex_};
    for (;;) {
        std::size_t pick = slots_.size();
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].busy ||
                std::find(excluded.begin(), excluded.end(), i) != excluded.end()) {
                continue;
            }
            if (slots_[i].session) {
                pick = i;
                break;
            }
            if (pick == slots_.size()) {
                pick = i;
            }
        }
        if (pick == slots_.size()) {
            if (!wait) {
                return Lease{EAGAIN};
            }
            idle_.wait(lock);
            continue;
        }
        Slot& s = slots_[pick];
        s.busy = true;
        if (s.session) {
            return Lease{this, pick, s.generation};
        }
        lock.unlock();
        int error = 0;
        std::unique_ptr<SmbSession> session = backend_.connect(error);
        lock.lock();
        if (!session) {
            s.busy = false;
            idle_.notify_all();
            return Lease{error != 0 ? error : EIO};
        }
        s.session = std::move(session);
        return Lease{this, pick, s.generation};
    }
}

SessionPool::Lease SessionPool::acquire_slot(std::size_t slot, std::uint64_t generation) {
    std::unique_lock lock{mutex_};
    if (slot >= slots_.size()) {
        return Lease{EIO};
    }
    Slot& s = slots_[slot];
    idle_.wait(lock, [&s] { return !s.busy; });
    if (!s.session || s.generation != generation) {
        return Lease{EIO};
    }
    s.busy = true;
    return Lease{this, slot, s.generation};
}

std::size_t SessionPool::connected() const {
    const std::lock_guard lock{mutex_};
    std::size_t n = 0;
    for (const Slot& s : slots_) {
        if (s.session) {
            ++n;
        }
    }
    return n;
}

}  // namespace lsw::host::platform
