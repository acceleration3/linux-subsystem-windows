#pragma once

#include "lsw/render/handler.hpp"

namespace lsw::render::handlers {

// Unpacks a ``kCmdBatch`` and replays its sub-commands through the same
// dispatcher that delivered it.
//
// The client accumulates one command buffer's ``vkCmd*`` stream and
// ships it as a single message; this takes it apart again. Nothing else
// about replay changes — each sub-command goes through the ordinary
// chain, in order, on the one replay thread — so a batched run and an
// unbatched run execute exactly the same sequence of calls against the
// driver. That is what makes the two comparable when something looks
// wrong.
//
// Holds a reference to the chain that owns it, which is a cycle on
// paper: the chain dispatches to this handler, and this handler
// dispatches back into the chain. It terminates because a batch never
// contains another batch — the client only ever puts async ``vkCmd*``
// into one.
class BatchHandler final : public Handler {
public:
    explicit BatchHandler(::lsw::protocol::FrameDispatcher& chain) noexcept : chain_(chain) {}

    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;

private:
    ::lsw::protocol::FrameDispatcher& chain_;
};

}  // namespace lsw::render::handlers
