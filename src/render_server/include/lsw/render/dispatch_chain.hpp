#pragma once

#include "lsw/render/handler.hpp"
#include "lsw/render/session.hpp"
#include "lsw/protocol/frame_dispatcher.hpp"

#include <memory>
#include <vector>

namespace lsw::render {

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

}  // namespace lsw::render
