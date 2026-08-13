#pragma once

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame_dispatcher.hpp"
#include "lsw/render/session.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace lsw::render {

// Result of a handler's attempt to process a frame.
enum class HandleResult {
    Handled,        // handler processed the frame; reply (if any) is written
    Pass,           // handler doesn't want this cmd_id; try the next one
    ProtocolError,  // malformed payload — caller closes the connection
};

// One link in a DispatchChain. Each handler owns a small command-id range
// and can decide to handle or pass. New protocol concerns (HELLO, PING,
// async-error, vkproto, recording, validation) are independent classes.
class Handler {
public:
    virtual ~Handler() = default;

    Handler() = default;
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;
    Handler(Handler&&) = delete;
    Handler& operator=(Handler&&) = delete;

    [[nodiscard]] virtual HandleResult handle(Session& session, std::uint32_t cmd_id,
                                              std::span<const std::uint8_t> payload,
                                              ::lsw::protocol::Buffer& reply) = 0;
};

}  // namespace lsw::render
