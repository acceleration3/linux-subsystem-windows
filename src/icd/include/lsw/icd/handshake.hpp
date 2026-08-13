#pragma once

#include "lsw/protocol/transport.hpp"

#include <cstdint>
#include <string>

#include "lsw/icd/client_hello.hpp"
#include "lsw/icd/server_hello_ack.hpp"

namespace lsw::icd {

// HELLO / HELLO_ACK exchange specified in docs/protocol.md.
//
// Wire format (HELLO, client -> server):
//   u32  magic       (== ::lsw::protocol::kProtocolMagic)
//   u16  proto_major (== ::lsw::protocol::kProtocolMajor)
//   u16  proto_minor (== ::lsw::protocol::kProtocolMinor)
//   u32  pid
//   varint app_name_len + app_name bytes
//
// Wire format (HELLO_ACK, server -> client):
//   u32  magic
//   u16  proto_major (negotiated)
//   u16  proto_minor (negotiated)
//   u64  context_id
//   u32  server_caps (reserved, must be 0 in Phase 2)


// Send a HELLO over ``transport`` and block for the HELLO_ACK. Returns 0 on
// success and one of the negative ``HandshakeError`` values on failure.
enum HandshakeError : int {
    kHandshakeOk = 0,
    kHandshakeTransportFailed = -1,
    kHandshakeBadMagic = -2,
    kHandshakeProtoMismatch = -3,
    kHandshakeMalformedReply = -4,
};

[[nodiscard]] int initiate_handshake(::lsw::protocol::Transport& transport, const ClientHello& info,
                                     ServerHelloAck* out);

}  // namespace lsw::icd
