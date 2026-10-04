#include "lsw/host/provision/install/post_install.hpp"

#include "lsw/base/log.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/frame_reader.hpp"

#include <QtConcurrent>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>

namespace lsw::host::provision::install {

namespace {

using linkproto::FrameType;

// The agent should never send a frame this large; a stream claiming
// one is bailed on rather than allowed to chew host memory.
constexpr std::size_t kMaxFrameLen = 4 * 1024 * 1024;
constexpr int kRecvChunk = 64 * 1024;
constexpr quint64 kProbeId = 1;

// Single-shot reachability probe. Opens a TCP connection, sends one
// ``system_info`` envelope, drains incoming frames until it sees the
// matching reply (skipping any log/installer events the agent pushes
// proactively), then returns true.
//
// QEMU's slirp hostfwd accepts host TCP handshakes immediately even
// when the guest isn't listening — so a successful ``connect()``
// doesn't prove the agent is up. The end-to-end JSON round trip
// here is what actually proves liveness.
[[nodiscard]] bool probe_reachable(quint32 cid, quint32 port) {
    platform::VsockClient client;
    if (client.connect(cid, port) != 0) {
        return false;
    }

    const nlohmann::json req = {{"id", kProbeId}, {"cmd", linkproto::cmd::kSystemInfo}};
    const std::string body = req.dump();
    QByteArray frame(static_cast<qsizetype>(linkproto::kHeaderSize + body.size()), '\0');
    auto* p = reinterpret_cast<std::uint8_t*>(frame.data());
    linkproto::write_header(p, FrameType::Control, static_cast<std::uint32_t>(body.size()));
    std::memcpy(p + linkproto::kHeaderSize, body.data(), body.size());
    if (client.send_all(frame) != 0) {
        return false;
    }

    linkproto::FrameReader reader;
    reader.reserve(kRecvChunk);
    bool answered = false;
    for (;;) {
        const QByteArray chunk = client.recv_some(kRecvChunk);
        if (chunk.isEmpty()) {
            return false;
        }
        reader.append(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                      static_cast<std::size_t>(chunk.size()));
        (void)reader.drain([&](FrameType type, const std::uint8_t* payload, std::size_t len) {
            if (type != FrameType::Control) {
                return true;
            }
            try {
                const auto j = nlohmann::json::parse(payload, payload + len);
                answered = j.is_object() && j.value("id", std::uint64_t{0}) == kProbeId &&
                           j.contains("result");
            } catch (const nlohmann::json::exception&) {
                // Malformed control payload — skip and keep draining.
            }
            return !answered;
        });
        if (answered) {
            return true;
        }
        // Whatever is left is one incomplete frame. Past the cap, it is
        // a frame we would refuse once it arrived, so stop reading now.
        if (reader.buffered() > linkproto::kHeaderSize + kMaxFrameLen) {
            return false;
        }
    }
}

}  // namespace

PostInstallWatcher::PostInstallWatcher(QUuid vm_uuid, quint32 cid, quint32 agent_port,
                                       QObject* parent)
    : QObject(parent), uuid_(vm_uuid), cid_(cid), port_(agent_port) {
    timer_.setInterval(3000);
    connect(&timer_, &QTimer::timeout, this, &PostInstallWatcher::on_tick);
}

void PostInstallWatcher::start() {
    if (!timer_.isActive()) {
        timer_.start();
    }
}

void PostInstallWatcher::stop() {
    timer_.stop();
}

void PostInstallWatcher::on_tick() {
    if (probe_in_flight_) {
        return;  // last probe still blocked on kernel connect — skip
    }
    probe_in_flight_ = true;
    (void)QtConcurrent::run([cid = cid_, port = port_] {
        return probe_reachable(cid, port);
    }).then(this, [this](bool reachable) {
        probe_in_flight_ = false;
        if (!reachable) {
            return;
        }
        LSW_INFO("guest agent reachable on cid={} port={} for VM {}", cid_, port_,
                 uuid_.toString().toStdString());
        timer_.stop();
        Q_EMIT ready(uuid_);
    });
}

}  // namespace lsw::host::provision::install
