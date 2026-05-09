#include "lsw/server/handlers/hello_handler.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace lsw::server::handlers {

namespace {

[[nodiscard]] std::uint64_t next_context_id() noexcept {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

HandleResult HelloHandler::handle(Session& session, std::uint32_t cmd_id,
                                  std::span<const std::uint8_t> payload,
                                  ::lsw::protocol::Buffer& reply) {
    if (cmd_id != ::lsw::protocol::kCmdHello) {
        return HandleResult::Pass;
    }
    using ::lsw::protocol::BufferReader;
    using ::lsw::protocol::kProtocolMagic;
    using ::lsw::protocol::kProtocolMajor;
    using ::lsw::protocol::kProtocolMinor;

    BufferReader r{payload};
    std::uint32_t magic = 0;
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t pid = 0;
    if (!r.read_pod(magic) || !r.read_pod(major) || !r.read_pod(minor) || !r.read_pod(pid)) {
        return HandleResult::ProtocolError;
    }
    if (magic != kProtocolMagic || major != kProtocolMajor) {
        return HandleResult::ProtocolError;
    }
    std::uint64_t app_name_len = 0;
    if (!r.read_varint(app_name_len)) {
        return HandleResult::ProtocolError;
    }
    std::string app_name;
    if (app_name_len > 0) {
        app_name.resize(app_name_len);
        if (!r.read(app_name.data(), app_name.size())) {
            return HandleResult::ProtocolError;
        }
    }

    session.set_context_id(next_context_id());
    session.set_app_name(std::move(app_name));
    session.set_client_pid(pid);
    session.mark_handshake_done();

    reply.write_pod<std::uint32_t>(kProtocolMagic);
    reply.write_pod<std::uint16_t>(kProtocolMajor);
    reply.write_pod<std::uint16_t>(kProtocolMinor);
    reply.write_pod<std::uint64_t>(session.context_id());
    reply.write_pod<std::uint32_t>(0u);  // server_caps reserved
    return HandleResult::Handled;
}

}  // namespace lsw::server::handlers
