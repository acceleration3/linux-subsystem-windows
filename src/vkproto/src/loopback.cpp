#include "lsw/vkproto/loopback.hpp"

namespace lsw::vkproto {

::lsw::protocol::TransportResult LoopbackTransport::send_sync(std::uint32_t cmd_id,
                                                              std::span<const std::uint8_t> payload,
                                                              ::lsw::protocol::Buffer* reply_out) {
    ::lsw::protocol::BufferReader r{payload};
    if (!dispatch_command(server_, cmd_id, r, *reply_out)) {
        return ::lsw::protocol::TransportResult::InvalidFrame;
    }
    return ::lsw::protocol::TransportResult::Ok;
}

::lsw::protocol::TransportResult
LoopbackTransport::send_async(std::uint32_t cmd_id, std::span<const std::uint8_t> payload) {
    ::lsw::protocol::Buffer ignored_reply;
    ::lsw::protocol::BufferReader r{payload};
    if (!dispatch_command(server_, cmd_id, r, ignored_reply)) {
        return ::lsw::protocol::TransportResult::InvalidFrame;
    }
    return ::lsw::protocol::TransportResult::Ok;
}

}  // namespace lsw::vkproto
