#pragma once

#include "lsw/render/handler.hpp"

namespace lsw::render::handlers {

// Server side of the Phase 1 memory shadow. Handles
//
//   kCmdMemoryWrite   client->server: u64 device, u64 memory,
//                                     u64 offset, u64 size, u8[size] bytes
//                     server->client: i32 vk_result
//
//   kCmdMemoryRead    client->server: u64 device, u64 memory,
//                                     u64 offset, u64 size
//                     server->client: i32 vk_result, u8[size] bytes
//
// The server vkMapMemory's the real ``VkDeviceMemory``, copies bytes,
// vkUnmapMemory's. Apps' write-after-flush ordering is preserved
// because the cmd stream serializes per-connection.
class MemoryHandler final : public Handler {
public:
    MemoryHandler() = default;

    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;
};

}  // namespace lsw::render::handlers
