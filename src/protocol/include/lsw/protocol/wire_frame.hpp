#pragma once

#include <cstdint>
#include <vector>

#include "lsw/protocol/frame_flags.hpp"

namespace lsw::protocol {

// In-memory representation of a wire frame. ``reply_token`` is meaningful
// iff ``flags & FrameFlags::needs_reply``.
struct Frame {
    std::uint32_t cmd_id = 0;
    std::uint8_t flags = 0;
    std::uint64_t reply_token = 0;
    std::vector<std::uint8_t> payload;
};

}  // namespace lsw::protocol
