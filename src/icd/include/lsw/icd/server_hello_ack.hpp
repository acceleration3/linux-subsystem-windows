#pragma once

#include "lsw/protocol/transport.hpp"
#include <cstdint>
#include <string>

namespace lsw::icd {

// Payload of the HELLO_ACK frame the render-server replies with.
struct ServerHelloAck {
    std::uint16_t proto_major = 0;
    std::uint16_t proto_minor = 0;
    std::uint64_t context_id = 0;
    std::uint32_t server_caps = 0;
};

}  // namespace lsw::icd
