#pragma once

#include <malloc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lsw::suites::sanity::cl {
class AlignedBlock {
public:
    AlignedBlock() = default;
    AlignedBlock(const AlignedBlock&) = delete;
    AlignedBlock& operator=(const AlignedBlock&) = delete;

    AlignedBlock(std::size_t size, std::size_t alignment) { allocate(size, alignment); }

    ~AlignedBlock() { release(); }

    bool allocate(std::size_t size, std::size_t alignment) {
        release();
        pointer_ = _aligned_malloc(size == 0 ? 1 : size, alignment < sizeof(void*) ? sizeof(void*) : alignment);
        size_ = size;
        if (pointer_ != nullptr) {
            std::memset(pointer_, 0, size);
        }
        return pointer_ != nullptr;
    }

    void release() {
        if (pointer_ != nullptr) {
            _aligned_free(pointer_);
            pointer_ = nullptr;
        }
    }

    [[nodiscard]] void* get() const { return pointer_; }

    [[nodiscard]] std::uint8_t* bytes() const { return static_cast<std::uint8_t*>(pointer_); }

    [[nodiscard]] std::uint32_t* words() const { return static_cast<std::uint32_t*>(pointer_); }

    [[nodiscard]] std::size_t size() const { return size_; }

    [[nodiscard]] explicit operator bool() const { return pointer_ != nullptr; }

private:
    void* pointer_ = nullptr;
    std::size_t size_ = 0;
};
}  // namespace lsw::suites::sanity::cl
