#pragma once

#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/listener.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace lsw::protocol {

// Which URL schemes exist on this platform.
//
// Parsing ``scheme://target`` is the same everywhere and lives in
// endpoint.cpp. What a scheme can be turned into is not: ``unix://``
// needs an AF_UNIX socket, and ``shm://`` needs a way to map a region —
// POSIX shared memory on the host, an IVSHMEM BAR in the guest.
//
// Splitting on that line rather than on ``#if defined(_WIN32)`` inside
// the URL code keeps the platform difference to one file per platform,
// which is where the second row of rule 1's table belongs: the guest's
// mapper is a different implementation of the same interface, not a
// different code path through the same function.
//
// Both return nullptr and set ``error_out`` for a scheme this platform
// does not have.

[[nodiscard]] std::unique_ptr<FrameChannel> platform_open_channel(std::string_view scheme,
                                                                  const std::string& target,
                                                                  std::string* error_out);

[[nodiscard]] std::unique_ptr<Listener> platform_open_listener(std::string_view scheme,
                                                               const std::string& target,
                                                               std::string* error_out);

}  // namespace lsw::protocol
