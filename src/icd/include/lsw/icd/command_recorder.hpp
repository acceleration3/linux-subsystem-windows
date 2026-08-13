#pragma once

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/transport.hpp"

#include <cstdint>

namespace lsw::icd {

// One command buffer's recorded ``vkCmd*`` stream, accumulated locally
// and shipped as a single message.
//
// This is a ``Transport`` that does not transport. The generated
// ``vkCmd*`` wrappers already serialise into a payload and call
// ``send_async``; handing them one of these instead of the connection's
// real transport appends the payload to a batch rather than putting it
// on the wire, and nothing in the generated code has to know.
//
// Why it is safe to have no lock at all, which is the entire point:
// Vulkan externally synchronises a ``VkCommandBuffer``, so exactly one
// thread may record into it at a time. The recorder therefore belongs to
// that thread for as long as recording lasts — N threads recording into
// N command buffers touch N separate recorders and contend for nothing.
// Only the flush at ``vkEndCommandBuffer`` touches the shared transport,
// once, with one large write instead of thousands of small ones.
//
// Ordering falls out rather than being arranged: per-buffer order holds
// because a single thread appends in call order, and
// recorded-before-submitted holds because End flushes before any submit
// can name the buffer.
class CommandRecorder final : public ::lsw::protocol::Transport {
public:
    CommandRecorder() = default;

    // Append one recorded command. Always succeeds — there is no peer
    // to refuse it.
    [[nodiscard]] ::lsw::protocol::TransportResult
    send_async(std::uint32_t cmd_id, std::span<const std::uint8_t> payload) override;

    // A command that wants a reply cannot be batched: the caller is
    // waiting for an answer that would not come until the flush.
    // Nothing in the ``vkCmd*`` family does this — they are all void and
    // all async — so reaching here means a command was routed to a
    // recorder that should not have been.
    [[nodiscard]] ::lsw::protocol::TransportResult
    send_sync(std::uint32_t cmd_id, std::span<const std::uint8_t> payload,
              ::lsw::protocol::Buffer* reply_out) override;

    // Drop everything recorded so far. ``vkBeginCommandBuffer`` and
    // ``vkResetCommandBuffer`` both mean "this buffer's contents are
    // gone"; a batch surviving either would replay commands the
    // application has discarded.
    void reset() noexcept;

    // Ship the batch upstream as one ``kCmdBatch`` message and clear it.
    // A recorder with nothing in it sends nothing.
    [[nodiscard]] ::lsw::protocol::TransportResult flush(::lsw::protocol::Transport& upstream);

    [[nodiscard]] std::uint32_t recorded_count() const noexcept { return count_; }
    [[nodiscard]] std::size_t recorded_bytes() const noexcept { return batch_.size(); }

private:
    ::lsw::protocol::Buffer batch_;
    std::uint32_t count_ = 0;
};

}  // namespace lsw::icd
