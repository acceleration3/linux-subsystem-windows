#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/protocol/transport.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace lsw::protocol {

// Parsed endpoint URL. Schemes:
//   ``unix:///path/to/sock``   — Unix-domain socket on the local filesystem.
//   ``vsock://<cid>:<port>``   — AF_VSOCK / HV_SOCKET (Phase 4+, not implemented).
//   ``tcp://<host>:<port>``    — TCP loopback for development (Phase 4+, not implemented).
//
// ``open_transport`` and ``open_listener`` parse the URL and return the
// appropriate concrete object. Returning ``nullptr`` means the URL was
// unparseable or the scheme isn't yet implemented; ``error_out`` (when
// non-null) gets a short human-readable explanation.
[[nodiscard]] std::unique_ptr<Transport> open_transport(std::string_view url,
                                                        std::string* error_out = nullptr);

[[nodiscard]] std::unique_ptr<Listener> open_listener(std::string_view url,
                                                      std::string* error_out = nullptr);

// Default endpoint when nothing else is configured.
//
// This differs by platform because the set of schemes does. On the host
// a Unix socket is the sensible default: it is inspectable, and the
// loopback harness lives on it. In the guest there is no such thing —
// ``shm://`` is the only scheme that exists there, and defaulting to a
// socket path meant a guest application loaded the ICD, got
// "unsupported transport scheme on Windows: unix", and reported
// VK_ERROR_INCOMPATIBLE_DRIVER. Which is a correct answer to the wrong
// question.
//
// The name after ``shm://`` is ignored in the guest: there is exactly
// one IVSHMEM device, so the region is not something to address. It is
// spelled as a URL anyway so that ``$LSW_SERVER_SOCKET`` overrides work
// identically on both sides.
#if defined(_WIN32)
inline constexpr std::string_view kDefaultEndpoint = "shm://guest";
#else
inline constexpr std::string_view kDefaultEndpoint = "unix:///tmp/lsw.sock";
#endif

}  // namespace lsw::protocol
