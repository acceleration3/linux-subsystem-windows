#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace lsw::protocol {

// Per-dispatch storage owner: anything the server-side deserializer needs to
// allocate (strings, struct arrays, optional ptr-to-struct payloads) is rooted
// here. The arena dies at the end of the dispatch and frees everything in one
// shot. Only trivially-destructible types are allowed; that covers every C
// type Vulkan asks for.
class Arena {
public:
    Arena() = default;
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = default;
    Arena& operator=(Arena&&) = default;
    ~Arena() = default;

    template<typename T>
    [[nodiscard]] T* alloc(std::size_t count = 1) {
        static_assert(std::is_trivially_destructible_v<T>,
                      "Arena only stores trivially-destructible types");
        if (count == 0) {
            return nullptr;
        }
        const std::size_t bytes = sizeof(T) * count;
        // Zero-init so deserializers can leave fields untouched without UB.
        auto block = std::make_unique<std::byte[]>(bytes);
        for (std::size_t i = 0; i < bytes; ++i) {
            block[i] = std::byte{0};
        }
        T* ptr = reinterpret_cast<T*>(block.get());
        blocks_.push_back(std::move(block));
        return ptr;
    }

    [[nodiscard]] char* alloc_chars(std::size_t count) { return alloc<char>(count); }

private:
    std::vector<std::unique_ptr<std::byte[]>> blocks_;
};

}  // namespace lsw::protocol
