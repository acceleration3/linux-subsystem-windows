#include "lsw/icd/handshake.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cstring>

namespace lsw::icd {

int initiate_handshake(::lsw::protocol::Transport& transport, const ClientHello& info,
                       ServerHelloAck* out) {
    using ::lsw::protocol::Buffer;
    using ::lsw::protocol::BufferReader;
    using ::lsw::protocol::kCmdHello;
    using ::lsw::protocol::kProtocolMagic;
    using ::lsw::protocol::kProtocolMajor;
    using ::lsw::protocol::kProtocolMinor;
    using ::lsw::protocol::TransportResult;

    Buffer payload;
    payload.write_pod<std::uint32_t>(kProtocolMagic);
    payload.write_pod<std::uint16_t>(kProtocolMajor);
    payload.write_pod<std::uint16_t>(kProtocolMinor);
    payload.write_pod<std::uint32_t>(info.pid);
    payload.write_varint(info.app_name.size());
    if (!info.app_name.empty()) {
        payload.write(info.app_name.data(), info.app_name.size());
    }

    Buffer reply;
    const TransportResult tr = transport.send_sync(kCmdHello, payload.bytes(), &reply);
    if (tr != TransportResult::Ok) {
        return kHandshakeTransportFailed;
    }

    BufferReader r{reply.bytes()};
    std::uint32_t magic = 0;
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint64_t context_id = 0;
    std::uint32_t caps = 0;
    if (!r.read_pod(magic) || !r.read_pod(major) || !r.read_pod(minor) || !r.read_pod(context_id) ||
        !r.read_pod(caps)) {
        return kHandshakeMalformedReply;
    }
    if (magic != kProtocolMagic) {
        return kHandshakeBadMagic;
    }
    if (major != kProtocolMajor) {
        return kHandshakeProtoMismatch;
    }
    if (out != nullptr) {
        out->proto_major = major;
        out->proto_minor = minor;
        out->context_id = context_id;
        out->server_caps = caps;
    }
    return kHandshakeOk;
}

}  // namespace lsw::icd
