#pragma once

#include <cstdint>

namespace lsw::protocol {

// Per-frame flag bits, set in FrameHeader::flags.
namespace FrameFlags {
inline constexpr std::uint8_t needs_reply = 1u << 0;
inline constexpr std::uint8_t has_bulk_ref = 1u << 1;
inline constexpr std::uint8_t trace = 1u << 2;
}  // namespace FrameFlags

}  // namespace lsw::protocol
