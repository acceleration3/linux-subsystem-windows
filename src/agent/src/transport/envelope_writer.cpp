#include "lsw/agent/transport/envelope_writer.hpp"

#include "lsw/agent/log.hpp"

#include "lsw/linkproto/byte_order.hpp"

// clang-format off
// Header order matters: <winsock2.h> before <windows.h>.
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

namespace lsw::agent::transport {

namespace {

// WinSock's "connection aborted" / "connection reset by peer". A peer
// that closed the window mid-send is routine, not a fault worth a WARN.
constexpr int kWsaConnAborted = 10053;
constexpr int kWsaConnReset   = 10054;

}  // namespace

EnvelopeWriter::EnvelopeWriter(std::intptr_t socket, std::string peer)
    : socket_(socket), peer_(std::move(peer)) {}

bool EnvelopeWriter::write_all(const std::uint8_t* p, int n) {
    auto s = static_cast<SOCKET>(socket_);
    while (n > 0) {
        const int w = ::send(s, reinterpret_cast<const char*>(p), n, 0);
        if (w < 0) {
            const int err = ::WSAGetLastError();
            if (err == kWsaConnAborted || err == kWsaConnReset) {
                LSW_AGENT_DEBUG("envelope_writer: send to {} refused (peer closed) wsa_err={}",
                                peer_, err);
            } else {
                LSW_AGENT_WARN("envelope_writer: send to {} failed wsa_err={}", peer_, err);
            }
            return false;
        }
        p += w;
        n -= w;
        bytes_sent_ += static_cast<std::uint64_t>(w);
    }
    return true;
}

bool EnvelopeWriter::send(linkproto::FrameType type, const std::uint8_t* payload,
                          std::size_t payload_len) {
    if (payload_len > std::numeric_limits<std::uint32_t>::max()) {
        LSW_AGENT_ERROR("envelope_writer: payload exceeds u32 length cap");
        return false;
    }
    std::uint8_t header[linkproto::kHeaderSize];
    linkproto::write_header(header, type, static_cast<std::uint32_t>(payload_len));
    // Two sends — TCP coalesces, and we avoid copying a big payload just
    // to bolt a 5-byte header in front of it.
    if (!write_all(header, static_cast<int>(linkproto::kHeaderSize))) {
        return false;
    }
    if (payload_len > 0 && !write_all(payload, static_cast<int>(payload_len))) {
        return false;
    }
    return true;
}

bool EnvelopeWriter::send_control(const nlohmann::json& message) {
    const std::string body = message.dump();
    return send(linkproto::FrameType::Control,
                reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
}


}  // namespace lsw::agent::transport
