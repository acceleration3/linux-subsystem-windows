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

}  // namespace lsw::protocol
