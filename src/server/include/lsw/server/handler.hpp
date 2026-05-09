#pragma once

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame_dispatcher.hpp"
#include "lsw/server/session.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace lsw::server {

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

// FrameDispatcher built from an ordered list of Handlers. The first handler
// to return Handled or ProtocolError stops the walk; if every handler
// returns Pass, the dispatch fails (unknown cmd_id).
//
// The chain is constructed once, before the connection's threads start;
// it is not modified concurrently with dispatch.
class DispatchChain final : public ::lsw::protocol::FrameDispatcher {
public:
    explicit DispatchChain(Session& session) noexcept : session_(session) {}

    // Append a handler. Order matters — earlier handlers win.
    DispatchChain& add(std::unique_ptr<Handler> h);

    [[nodiscard]] bool dispatch(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
                                ::lsw::protocol::Buffer& reply) override;

    [[nodiscard]] Session& session() noexcept { return session_; }

private:
    Session& session_;
    std::vector<std::unique_ptr<Handler>> handlers_;
};

}  // namespace lsw::server
