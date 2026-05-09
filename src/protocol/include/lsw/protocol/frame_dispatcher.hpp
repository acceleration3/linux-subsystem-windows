#pragma once

#include "lsw/protocol/buffer.hpp"

#include <cstdint>
#include <span>

namespace lsw::protocol {

// Vulkan-agnostic dispatch hook. The transport reads frames off the wire,
// hands them to a FrameDispatcher, writes the reply back. lsw_vkproto plugs
// in an implementation that translates frames into ServerDispatch calls.
class FrameDispatcher {
public:
    virtual ~FrameDispatcher() = default;

    FrameDispatcher() = default;
    FrameDispatcher(const FrameDispatcher&) = delete;
    FrameDispatcher& operator=(const FrameDispatcher&) = delete;
    FrameDispatcher(FrameDispatcher&&) = delete;
    FrameDispatcher& operator=(FrameDispatcher&&) = delete;

    // Returns false on protocol error (caller closes the connection).
    [[nodiscard]] virtual bool dispatch(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
                                        Buffer& reply) = 0;
};

}  // namespace lsw::protocol
