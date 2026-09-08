#pragma once

#include <cstring>

namespace lsw::linkproto {

// Read and write one field of a shared-memory structure.
//
// Through memcpy rather than a pointer cast: the region is a mapped BAR
// and the two ends compile it separately, so nothing may depend on a
// struct's alignment or padding as this compiler chose to lay it out.
// The layouts are pinned by their offset constants; these are how the
// bytes at those offsets are actually touched.

template <typename T>
[[nodiscard]] inline T load_field(const void* p) noexcept {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

template <typename T>
inline void store_field(void* p, T v) noexcept {
    std::memcpy(p, &v, sizeof(T));
}

}  // namespace lsw::linkproto
