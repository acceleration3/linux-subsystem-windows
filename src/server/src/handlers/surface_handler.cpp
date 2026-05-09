#include "lsw/server/handlers/surface_handler.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cstdint>
#include <cstring>

namespace lsw::server::handlers {

namespace {

[[nodiscard]] VkInstance instance_from_u64(std::uint64_t raw) noexcept {
    VkInstance out = VK_NULL_HANDLE;
    static_assert(sizeof(out) == sizeof(raw));
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

}  // namespace

HandleResult SurfaceHandler::handle(Session& /*session*/, std::uint32_t cmd_id,
                                    std::span<const std::uint8_t> payload,
                                    ::lsw::protocol::Buffer& reply) {
    using ::lsw::protocol::BufferReader;

    if (cmd_id == ::lsw::protocol::kCmdCreateXcbSurface) {
        BufferReader r{payload};
        std::uint64_t inst_raw = 0;
        std::uint32_t window = 0;
        if (!r.read_pod(inst_raw) || !r.read_pod(window)) {
            return HandleResult::ProtocolError;
        }
        std::uint64_t synthetic_id = 0;
        const VkResult rc =
            mgr_.create_from_xcb(instance_from_u64(inst_raw), window, &synthetic_id);
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(rc));
        reply.write_pod<std::uint64_t>(synthetic_id);
        return HandleResult::Handled;
    }
    if (cmd_id == ::lsw::protocol::kCmdDestroySurface) {
        BufferReader r{payload};
        std::uint64_t inst_raw = 0;
        std::uint64_t synthetic_id = 0;
        if (!r.read_pod(inst_raw) || !r.read_pod(synthetic_id)) {
            return HandleResult::ProtocolError;
        }
        mgr_.destroy(instance_from_u64(inst_raw), synthetic_id);
        reply.write_pod<std::int32_t>(0);
        return HandleResult::Handled;
    }
    return HandleResult::Pass;
}

}  // namespace lsw::server::handlers
