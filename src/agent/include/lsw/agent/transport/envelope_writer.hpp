#pragma once

#include "lsw/linkproto/envelope.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace lsw::agent::transport {
class EnvelopeWriter {
public:
    EnvelopeWriter(std::intptr_t socket, std::string peer);

    EnvelopeWriter(const EnvelopeWriter&) = delete;
    EnvelopeWriter& operator=(const EnvelopeWriter&) = delete;

    [[nodiscard]] bool send(linkproto::FrameType type, const std::uint8_t* payload,
                            std::size_t payload_len);

    [[nodiscard]] bool send_control(const nlohmann::json& message);


    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }

private:
    [[nodiscard]] bool write_all(const std::uint8_t* p, int n);

    std::intptr_t socket_;
    std::string peer_;
    std::uint64_t bytes_sent_ = 0;
};

}
