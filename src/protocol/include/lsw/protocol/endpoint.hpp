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

// Default endpoint for the ICD client and render-server when nothing else
// is configured. Today this is a Unix-domain socket in /tmp; vsock takes
// over once Phase 4 lands.
inline constexpr std::string_view kDefaultEndpoint = "unix:///tmp/lsw.sock";

}  // namespace lsw::protocol
