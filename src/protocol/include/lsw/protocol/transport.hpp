#pragma once

#include "lsw/protocol/buffer.hpp"

#include <cstdint>
#include <span>

namespace lsw::protocol {

enum class TransportResult : std::int32_t {
    Ok = 0,
    Disconnected = -1,
    InvalidFrame = -2,
    Timeout = -3,
};

// Transport abstraction the generated client code calls into.
//
// The transport handles framing: callers pass the serialized payload bytes
// and a logical command ID. For sync calls the transport blocks for a
// reply and writes it into ``reply_out``. The first ``int32_t`` of every
// reply is a dispatch result code (typically a ``VkResult``); the rest is
// command-specific.
class Transport {
public:
    virtual ~Transport() = default;

    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    [[nodiscard]] virtual TransportResult
    send_sync(std::uint32_t cmd_id, std::span<const std::uint8_t> payload, Buffer* reply_out) = 0;

    [[nodiscard]] virtual TransportResult send_async(std::uint32_t cmd_id,
                                                     std::span<const std::uint8_t> payload) = 0;
};

}  // namespace lsw::protocol
