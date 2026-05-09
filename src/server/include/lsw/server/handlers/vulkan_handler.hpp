#pragma once

#include "lsw/server/handler.hpp"
#include "vk_dispatch_server.h"  // generated

namespace lsw::server::handlers {

// Routes Vulkan command frames into the codegen-emitted ServerDispatch.
//
// The handler owns no state of its own; per-connection Vulkan state
// (handle remap, instance/device lifetimes, etc.) lives on the Session
// or inside the ServerDispatch implementation.
class VulkanHandler final : public Handler {
public:
    explicit VulkanHandler(::lsw::vkproto::ServerDispatch& server) noexcept : server_(server) {}

    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;

private:
    ::lsw::vkproto::ServerDispatch& server_;
};

}  // namespace lsw::server::handlers
