#pragma once

#include "lsw/render/handler.hpp"

namespace lsw::render::handlers {

// Owns the protocol-level HELLO/HELLO_ACK exchange.
//
// On HELLO it reads the client's app metadata, validates magic +
// proto_major, fills in the Session (context_id, app_name, pid), writes
// the HELLO_ACK reply payload, and marks the session handshaken.
//
// Returns ``Pass`` for any other cmd_id so later handlers can claim them.
class HelloHandler final : public Handler {
public:
    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;
};

}  // namespace lsw::render::handlers
