#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace lsw::protocol {

// Bounded forward reader over an existing byte range.
class BufferReader {
public:
    BufferReader(const void* data, std::size_t n) noexcept
        : p_(static_cast<const std::uint8_t*>(data)), end_(p_ + n) {}

    explicit BufferReader(std::span<const std::uint8_t> s) noexcept
        : BufferReader(s.data(), s.size()) {}

    [[nodiscard]] bool eof() const noexcept { return p_ == end_; }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return static_cast<std::size_t>(end_ - p_);
    }

    // Bytes not yet read, as a view. Used to hand a nested message to
    // another parser without copying it out first.
    [[nodiscard]] std::span<const std::uint8_t> rest() const noexcept { return {p_, remaining()}; }

    // Advance past ``n`` bytes without reading them.
    [[nodiscard]] bool skip(std::size_t n) noexcept {
        if (remaining() < n) {
            return false;
        }
        p_ += n;
        return true;
    }

    [[nodiscard]] bool read(void* out, std::size_t n) noexcept {
        if (remaining() < n) {
            return false;
        }
        std::memcpy(out, p_, n);
        p_ += n;
        return true;
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] bool read_pod(T& v) noexcept {
        return read(&v, sizeof(T));
    }

    [[nodiscard]] bool read_varint(std::uint64_t& out) noexcept {
        std::uint64_t result = 0;
        unsigned shift = 0;
        while (p_ != end_) {
            const std::uint8_t byte = *p_++;
            // Reject overlong / overflowing encodings; max 10 bytes for 64-bit LEB128.
            if (shift >= 64) {
                return false;
            }
            result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                out = result;
                return true;
            }
            shift += 7;
        }
        return false;
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

}  // namespace lsw::protocol
