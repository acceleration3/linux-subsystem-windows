#pragma once

#include "lsw/linkproto/envelope.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::agent::transport {

// The write half of one connection: turns payloads into envelopes and
// pushes them at the socket, counting bytes as it goes.
//
// Split out of the connection loop because "how a message becomes bytes"
// and "what messages there are" are different jobs, and only the former
// needs to know about WinSock. The framing itself comes from
// ``lsw/linkproto`` — shared with the manager, not restated here.
class EnvelopeWriter {
public:
    // ``socket`` is a WinSock SOCKET; kept as intptr_t so this header
    // stays free of <winsock2.h> (which has to be included before
    // <windows.h> everywhere it appears). ``peer`` is a display string
    // used only in log lines.
    EnvelopeWriter(std::intptr_t socket, std::string peer);

    EnvelopeWriter(const EnvelopeWriter&) = delete;
    EnvelopeWriter& operator=(const EnvelopeWriter&) = delete;

    // Send one envelope. Returns false once the peer is gone — the
    // caller's cue to abandon the connection.
    [[nodiscard]] bool send(linkproto::FrameType type, const std::uint8_t* payload,
                            std::size_t payload_len);

    // Send a JSON object as a control envelope.
    [[nodiscard]] bool send_control(const nlohmann::json& message);

    // Send the "this window is gone" envelope for ``hwnd``.
    [[nodiscard]] bool send_frame_gone(std::uint64_t hwnd);

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }

private:
    [[nodiscard]] bool write_all(const std::uint8_t* p, int n);

    std::intptr_t socket_;
    std::string peer_;
    std::uint64_t bytes_sent_ = 0;
};

}  // namespace lsw::agent::transport
