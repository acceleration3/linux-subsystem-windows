#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace lsw::protocol {

// Append-only byte buffer used by the serializer side.
class Buffer {
public:
    Buffer() = default;

    explicit Buffer(std::size_t reserve_bytes) { data_.reserve(reserve_bytes); }

    void clear() noexcept { data_.clear(); }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return {data_.data(), data_.size()};
    }

    void write(const void* p, std::size_t n) {
        const auto* src = static_cast<const std::uint8_t*>(p);
        data_.insert(data_.end(), src, src + n);
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void write_pod(const T& v) {
        write(&v, sizeof(T));
    }

    // LEB128 unsigned varint.
    void write_varint(std::uint64_t v) {
        while (v >= 0x80) {
            data_.push_back(static_cast<std::uint8_t>(v) | 0x80);
            v >>= 7;
        }
        data_.push_back(static_cast<std::uint8_t>(v));
    }

private:
    std::vector<std::uint8_t> data_;
};

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
