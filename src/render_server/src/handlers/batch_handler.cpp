#include "lsw/render/handlers/batch_handler.hpp"

#include "lsw/base/log.hpp"
#include "lsw/protocol/buffer_reader.hpp"
#include "lsw/protocol/protocol_constants.hpp"

#include <cstdint>

namespace lsw::render::handlers {

HandleResult BatchHandler::handle(Session& /*session*/, std::uint32_t cmd_id,
                                  std::span<const std::uint8_t> payload,
                                  ::lsw::protocol::Buffer& /*reply*/) {
    if (cmd_id != ::lsw::protocol::kCmdBatch) {
        return HandleResult::Pass;
    }

    ::lsw::protocol::BufferReader r{payload};
    std::uint32_t count = 0;
    if (!r.read_pod(count)) {
        return HandleResult::ProtocolError;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t sub_id = 0;
        std::uint32_t len = 0;
        if (!r.read_pod(sub_id) || !r.read_pod(len)) {
            LSW_ERROR("batch truncated at entry {}/{}", i, count);
            return HandleResult::ProtocolError;
        }
        if (r.remaining() < len) {
            LSW_ERROR("batch entry {}/{} claims {} bytes, {} left", i, count, len,
                      r.remaining());
            return HandleResult::ProtocolError;
        }
        // A batch inside a batch would recurse; the client never
        // produces one, and refusing it here keeps that guarantee from
        // being something a peer can revoke.
        if (sub_id == ::lsw::protocol::kCmdBatch) {
            LSW_ERROR("batch contains a nested batch");
            return HandleResult::ProtocolError;
        }

        // A borrowed view into the payload we already hold — the
        // sub-command's bytes are not copied out.
        const std::span<const std::uint8_t> sub = r.rest().first(len);
        if (!r.skip(len)) {
            return HandleResult::ProtocolError;
        }

        ::lsw::protocol::Buffer sub_reply;
        if (!chain_.dispatch(sub_id, sub, sub_reply)) {
            LSW_ERROR("batch entry {}/{} (cmd_id=0x{:08x}) failed to dispatch", i, count,
                      sub_id);
            return HandleResult::ProtocolError;
        }
        // Nothing in a batch has a reply to send: only async commands
        // are ever recorded, and the frame that carried the batch did
        // not ask for one either.
    }

    if (!r.eof()) {
        LSW_WARN("batch had {} trailing bytes", r.remaining());
    }
    return HandleResult::Handled;
}

}  // namespace lsw::render::handlers
