#include "lsw/agent/session/pipe_codec.hpp"

#include "lsw/linkproto/envelope.hpp"

namespace lsw::agent::session {

std::string PipeCodec::encode(const nlohmann::json& message) {
    const std::string body = message.dump();
    std::string out(linkproto::kHeaderSize + body.size(), '\0');
    linkproto::write_header(reinterpret_cast<std::uint8_t*>(out.data()),
                            linkproto::FrameType::Control, static_cast<std::uint32_t>(body.size()));
    out.replace(linkproto::kHeaderSize, body.size(), body);
    return out;
}

void PipeCodec::feed(const std::uint8_t* data, std::size_t len) {
    reader_.append(data, len);
}

bool PipeCodec::drain(std::vector<nlohmann::json>& out) {
    const bool intact =
        reader_.drain([&](linkproto::FrameType type, const std::uint8_t* payload, std::size_t len) {
            if (type != linkproto::FrameType::Control) {
                return true;
            }
            try {
                out.push_back(nlohmann::json::parse(payload, payload + len));
            } catch (const nlohmann::json::exception&) {
                ++malformed_;
            }
            return true;
        });
    if (!intact) {
        return false;
    }
    // Whatever remains is one incomplete frame. Past the cap it is a
    // frame that would be refused once it arrived — a corrupt length — so
    // stop reading now rather than buffer whatever it claims.
    return reader_.buffered() <= linkproto::kHeaderSize + kMaxMessageBytes;
}

bool PipeCodec::is_request(const nlohmann::json& m) {
    return m.is_object() && !m.contains("event") && m.contains("id") && m.contains("cmd") &&
           m["cmd"].is_string();
}

bool PipeCodec::is_reply(const nlohmann::json& m) {
    return m.is_object() && !m.contains("event") && !m.contains("cmd") && m.contains("id") &&
           (m.contains("result") || m.contains("error"));
}

bool PipeCodec::is_event(const nlohmann::json& m) {
    return m.is_object() && m.contains("event") && m["event"].is_string();
}

}  // namespace lsw::agent::session
