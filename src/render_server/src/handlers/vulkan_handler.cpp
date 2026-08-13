#include "lsw/render/dispatch_chain.hpp"
#include "lsw/render/handlers/vulkan_handler.hpp"

#include "lsw/base/log.hpp"
#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/buffer_reader.hpp"
#include "vk_command_names.h"

namespace lsw::render::handlers {

HandleResult VulkanHandler::handle(Session& /*session*/, std::uint32_t cmd_id,
                                   std::span<const std::uint8_t> payload,
                                   ::lsw::protocol::Buffer& reply) {
    LSW_TRACE("server <- {} (cmd_id=0x{:08x}, payload={} bytes)",
              ::lsw::vkproto::cmd_id_to_string(cmd_id), cmd_id, payload.size());
    ::lsw::protocol::BufferReader r{payload};
    if (!::lsw::vkproto::dispatch_command(server_, cmd_id, r, reply)) {
        // The generated dispatcher returns false for unknown cmd_ids
        // OR for malformed payload. We translate "false" into ``Pass``
        // so other handlers can still claim non-Vulkan command IDs;
        // a malformed payload would have been a real failure but is
        // hard to distinguish from "unknown" at this layer without
        // richer return info from the codegen. Either way the
        // DispatchChain logs an UNKNOWN error if no handler claims.
        return HandleResult::Pass;
    }
    return HandleResult::Handled;
}

}  // namespace lsw::render::handlers
