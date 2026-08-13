#pragma once

#include <string>

namespace lsw::protocol {

// Connecting end of an ``AF_UNIX`` ``SOCK_STREAM`` socket.
//
// This is all that is left of ``UnixSocketTransport``. It used to be a
// whole ``Transport``: write a frame, block reading the reply, match the
// two by position. That was the A1 bug — one transport per
// ``VkInstance``, several application threads, and no way to tell whose
// reply had arrived.
//
// ``ChannelTransport`` does the job now, for the socket and for shared
// memory alike, with a token map instead of an ordering assumption. What
// remains here is the one thing that was genuinely about sockets:
// getting a connected file descriptor.
//
// Returns the fd, or -errno.
[[nodiscard]] int connect_unix_socket(const std::string& path);

}  // namespace lsw::protocol
