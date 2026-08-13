#include "lsw/protocol/region_arena.hpp"

#include <algorithm>

namespace lsw::protocol {

namespace {

[[nodiscard]] constexpr std::uint64_t align_up(std::uint64_t v, std::uint64_t alignment) {
    return (v + alignment - 1) & ~(alignment - 1);
}

}  // namespace

void RegionArena::bind(SharedRegion& window) noexcept {
    const std::lock_guard lk{mutex_};
    base_ = static_cast<std::uint8_t*>(window.base());
    size_ = static_cast<std::uint64_t>(window.size());
    live_.clear();
}

std::uint64_t RegionArena::allocate(std::uint64_t bytes) {
    if (bytes == 0) {
        return kNoOffset;
    }
    const std::uint64_t need = align_up(bytes, kAlignment);

    const std::lock_guard lk{mutex_};
    if (base_ == nullptr || need > size_) {
        return kNoOffset;
    }

    // First fit over the gaps: before the first block, between
    // neighbours, then after the last.
    std::uint64_t cursor = 0;
    for (std::size_t i = 0; i <= live_.size(); ++i) {
        const std::uint64_t next_start = (i < live_.size()) ? live_[i].offset : size_;
        if (next_start >= cursor && next_start - cursor >= need) {
            live_.insert(live_.begin() + static_cast<std::ptrdiff_t>(i), Block{cursor, need});
            return cursor;
        }
        if (i < live_.size()) {
            cursor = live_[i].offset + live_[i].size;
        }
    }
    // The arena is full. An ordinary answer: the caller copies instead.
    return kNoOffset;
}

void RegionArena::release(std::uint64_t offset) noexcept {
    const std::lock_guard lk{mutex_};
    const auto it = std::lower_bound(live_.begin(), live_.end(), offset,
                                     [](const Block& b, std::uint64_t o) { return b.offset < o; });
    if (it != live_.end() && it->offset == offset) {
        live_.erase(it);
    }
}

std::uint8_t* RegionArena::pointer_at(std::uint64_t offset) noexcept {
    const std::lock_guard lk{mutex_};
    if (base_ == nullptr) {
        return nullptr;
    }
    const auto it = std::lower_bound(live_.begin(), live_.end(), offset,
                                     [](const Block& b, std::uint64_t o) { return b.offset < o; });
    if (it == live_.end() || it->offset != offset) {
        return nullptr;
    }
    return base_ + offset;
}

std::uint64_t RegionArena::bytes_in_use() const noexcept {
    const std::lock_guard lk{mutex_};
    std::uint64_t total = 0;
    for (const auto& b : live_) {
        total += b.size;
    }
    return total;
}

}  // namespace lsw::protocol
