#pragma once

#include <cstdint>

namespace lsw::protocol {

// Outcome of a frame read or write. Every I/O entry point returns one
// of these rather than errno: the transports differ, the failure modes
// callers care about do not.
enum class IoResult : std::int32_t {
    Ok = 0,
    Disconnected = -1,
    InvalidFrame = -2,
    Timeout = -3,
};

}  // namespace lsw::protocol
