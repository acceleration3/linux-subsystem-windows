#pragma once

#include "lsw/render/handler.hpp"
#include "lsw/render/surface_manager.hpp"

namespace lsw::render::handlers {

// Owns the non-codegen surface protocol commands:
//
//   kCmdCreateXcbSurface   client->server: u64 instance_handle, u32 window
//                          server->client: i32 vk_result, u64 synthetic_id
//
//   kCmdDestroySurface     client->server: u64 instance_handle, u64 synthetic_id
//                          server->client: i32 vk_result (always 0)
//
// VkSurface creation has to live outside the codegen because the
// platform-specific create-info structs (xcb_connection_t* / wl_surface*)
// can't cross a process boundary. The client just hands over the X11
// window id; the server opens its own xcb connection to the same X
// server and creates the surface there.
class SurfaceHandler final : public Handler {
public:
    explicit SurfaceHandler(SurfaceManager& mgr) noexcept : mgr_(mgr) {}

    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;

private:
    SurfaceManager& mgr_;
};

}  // namespace lsw::render::handlers
