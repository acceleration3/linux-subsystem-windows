#pragma once

#include "lsw/linkproto/frame_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace lsw::agent::session {
class PipeCodec {
public:
    static constexpr std::size_t kMaxMessageBytes = 16U * 1024U * 1024U;

    [[nodiscard]] static std::string encode(const nlohmann::json& message);

    void feed(const std::uint8_t* data, std::size_t len);

    [[nodiscard]] bool drain(std::vector<nlohmann::json>& out);

    [[nodiscard]] std::size_t malformed() const noexcept { return malformed_; }

    [[nodiscard]] static bool is_request(const nlohmann::json& m);
    [[nodiscard]] static bool is_reply(const nlohmann::json& m);
    [[nodiscard]] static bool is_event(const nlohmann::json& m);

private:
    linkproto::FrameReader reader_;
    std::size_t malformed_ = 0;
};

}
