#pragma once

#include "lsw/protocol/shared_region.hpp"

#include <cstdint>
#include <string>

namespace lsw::protocol {

// A window onto part of another ``SharedRegion``.
//
// The region is divided into channels, and a channel — not the region —
// is what a ``ShmemFrameChannel`` binds to. Both peers need to say "the
// cmdstream channel of this region", and both say it the same way:
// ``find_channel`` for the offset and size, then a ``SubRegion``.
//
// Worth being explicit that this is not the VM's half of anything. It is
// used identically on the host and in the guest, over a
// ``PosixShmRegion`` and over an ``IvshmemRegion``, because dividing a
// region into channels is a property of the layout, not of who mapped
// it. The environment-specific row in the table is still exactly one:
// which ``SharedRegion`` sits underneath.
//
// Borrows the parent — the parent must outlive the view.
class SubRegion final : public SharedRegion {
public:
    SubRegion() = default;

    SubRegion(SharedRegion& parent, std::uint64_t offset, std::uint64_t size) {
        // A window that did not fit simply stays invalid; ``valid()``
        // reports it, which is the same answer ``bind`` gives.
        static_cast<void>(bind(parent, offset, size));
    }

    // Returns false when the window does not lie wholly inside the
    // parent — a channel table that says otherwise is corrupt, or from a
    // peer that disagrees about the region size, and either way reading
    // through it would be reading whatever follows the mapping.
    [[nodiscard]] bool bind(SharedRegion& parent, std::uint64_t offset, std::uint64_t size) {
        base_ = nullptr;
        size_ = 0;
        if (!parent.valid() || size == 0) {
            return false;
        }
        const auto parent_size = static_cast<std::uint64_t>(parent.size());
        if (offset > parent_size || size > parent_size - offset) {
            return false;
        }
        base_ = static_cast<std::uint8_t*>(parent.base()) + offset;
        size_ = static_cast<std::size_t>(size);
        description_ = parent.description() + "+0x" + to_hex(offset);
        return true;
    }

    [[nodiscard]] void* base() const noexcept override { return base_; }
    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] std::string description() const override { return description_; }

private:
    [[nodiscard]] static std::string to_hex(std::uint64_t v) {
        static constexpr std::string_view kDigits = "0123456789abcdef";
        if (v == 0) {
            return "0";
        }
        std::string s;
        while (v != 0) {
            s.insert(s.begin(), kDigits[v & 0xF]);
            v >>= 4;
        }
        return s;
    }

    void* base_ = nullptr;
    std::size_t size_ = 0;
    std::string description_ = "(unbound)";
};

}  // namespace lsw::protocol
