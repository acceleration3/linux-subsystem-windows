#include "lsw/icd/command_recorder.hpp"

#include "lsw/base/log.hpp"
#include "lsw/protocol/protocol_constants.hpp"

namespace lsw::icd {

using ::lsw::protocol::TransportResult;

TransportResult CommandRecorder::send_async(std::uint32_t cmd_id,
                                            std::span<const std::uint8_t> payload) {
    batch_.write_pod<std::uint32_t>(cmd_id);
    batch_.write_pod<std::uint32_t>(static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        batch_.write(payload.data(), payload.size());
    }
    ++count_;
    return TransportResult::Ok;
}

TransportResult CommandRecorder::send_sync(std::uint32_t cmd_id,
                                           std::span<const std::uint8_t> /*payload*/,
                                           ::lsw::protocol::Buffer* /*reply_out*/) {
    // Deliberately not "flush, then forward": that would silently make a
    // routing mistake work, and the next symptom would be an ordering
    // bug much further away. Every batchable command is void and async.
    LSW_ERROR("command recorder got a synchronous command (cmd_id=0x{:08x}); "
              "only async vkCmd* may be batched",
              cmd_id);
    return TransportResult::InvalidFrame;
}

void CommandRecorder::reset() noexcept {
    batch_.clear();
    count_ = 0;
}

TransportResult CommandRecorder::flush(::lsw::protocol::Transport& upstream) {
    if (count_ == 0) {
        return TransportResult::Ok;
    }
    ::lsw::protocol::Buffer message;
    message.write_pod<std::uint32_t>(count_);
    message.write(batch_.data(), batch_.size());

    const auto rc = upstream.send_async(::lsw::protocol::kCmdBatch, message.bytes());
    LSW_TRACE("flushed {} recorded commands ({} bytes) as one batch", count_, batch_.size());
    reset();
    return rc;
}

}  // namespace lsw::icd
