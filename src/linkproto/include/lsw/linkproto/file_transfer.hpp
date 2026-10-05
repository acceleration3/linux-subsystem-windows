#pragma once

#include <cstddef>
#include <cstdint>

namespace lsw::linkproto {

inline constexpr std::size_t kFileChunkBytes = 192 * 1024;
inline constexpr std::uint64_t kMaxUploadBytes = 16ULL * 1024 * 1024 * 1024;

}  // namespace lsw::linkproto
