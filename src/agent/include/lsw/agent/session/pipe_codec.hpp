#pragma once

#include "lsw/linkproto/frame_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace lsw::agent::session {

// The framing between the service and a session helper: the manager
// link's own envelope (``lsw/linkproto/envelope.hpp``), carrying the same
// JSON messages — requests, replies and events — over a named pipe
// instead of a socket. One format for both hops means a request is
// forwarded with its params untouched and an event comes back up
// unchanged.
class PipeCodec {
public:
    // Larger than any reply a helper produces (a deep UIA tree dump is
    // the biggest); a length past it means the stream is corrupt.
    static constexpr std::size_t kMaxMessageBytes = 16U * 1024U * 1024U;

    // One control envelope holding ``message``.
    [[nodiscard]] static std::string encode(const nlohmann::json& message);

    // Buffer bytes read from the pipe.
    void feed(const std::uint8_t* data, std::size_t len);

    // Append every complete message buffered to ``out``. Malformed JSON
    // and non-control envelopes are skipped. Returns false once the
    // stream is unusable — a frame announcing more than
    // ``kMaxMessageBytes`` — after which the pipe must be dropped.
    [[nodiscard]] bool drain(std::vector<nlohmann::json>& out);

    // Messages skipped for not parsing, for the log.
    [[nodiscard]] std::size_t malformed() const noexcept { return malformed_; }

    // Classification of what arrived.
    [[nodiscard]] static bool is_request(const nlohmann::json& m);
    [[nodiscard]] static bool is_reply(const nlohmann::json& m);
    [[nodiscard]] static bool is_event(const nlohmann::json& m);

private:
    linkproto::FrameReader reader_;
    std::size_t malformed_ = 0;
};

}  // namespace lsw::agent::session
