#pragma once

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/transport.hpp"
#include "vk_dispatch_server.h"

#include <cstdint>
#include <span>

namespace lsw::vkproto {

// In-process transport: client writes go directly into the server's dispatch.
//
// Used by tests to validate that serialization round-trips end-to-end.
// Real transports live in src/server (vsock/unix-socket acceptor) and
// src/client (vsock/unix-socket connect).
class LoopbackTransport final : public ::lsw::protocol::Transport {
public:
    explicit LoopbackTransport(ServerDispatch& server) noexcept : server_(server) {}

    [[nodiscard]] ::lsw::protocol::TransportResult
    send_sync(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
              ::lsw::protocol::Buffer* reply_out) override;

    [[nodiscard]] ::lsw::protocol::TransportResult
    send_async(std::uint32_t cmd_id, std::span<const std::uint8_t> payload) override;

private:
    ServerDispatch& server_;
};

}  // namespace lsw::vkproto
