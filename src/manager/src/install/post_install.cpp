#include "lsw/manager/install/post_install.hpp"

#include "lsw/log.hpp"

#include <QFutureWatcher>
#include <QtConcurrent>

#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

namespace lsw::manager::install {

namespace {

// Wire-format constants — mirror of the agent's host_endpoint.cpp:
//   [type:u8][len:u32-BE][payload:len bytes]
//   type=1 control (JSON)
constexpr std::uint8_t kFrameTypeControl = 1;
constexpr qsizetype   kFrameHeaderSize  = 5;
constexpr std::uint32_t kMaxFrameLen    = 4 * 1024 * 1024;  // 4 MiB sanity cap
constexpr quint64     kProbeId          = 1;

void write_u32_be(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFU);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFU);
    p[2] = static_cast<std::uint8_t>((v >>  8) & 0xFFU);
    p[3] = static_cast<std::uint8_t>( v        & 0xFFU);
}

std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) <<  8)
         |  static_cast<std::uint32_t>(p[3]);
}

// Read exactly ``n`` bytes from ``client`` into ``buf``. Returns
// false if the connection closes mid-read.
[[nodiscard]] bool recv_exact(vm::VsockClient& client, QByteArray& buf, qsizetype n) {
    while (buf.size() < n) {
        const auto chunk = client.recv_some(static_cast<int>(n - buf.size()));
        if (chunk.isEmpty()) {
            return false;
        }
        buf.append(chunk);
    }
    return true;
}

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
    vm::VsockClient client;
    if (client.connect(cid, port) != 0) {
        return false;
    }

    const nlohmann::json req = {{"id", kProbeId}, {"cmd", "system_info"}};
    const std::string body = req.dump();
    QByteArray frame;
    frame.resize(kFrameHeaderSize + static_cast<qsizetype>(body.size()));
    auto* p = reinterpret_cast<std::uint8_t*>(frame.data());
    p[0] = kFrameTypeControl;
    write_u32_be(p + 1, static_cast<std::uint32_t>(body.size()));
    std::memcpy(p + kFrameHeaderSize, body.data(), body.size());
    if (client.send_all(frame) != 0) {
        return false;
    }

    QByteArray rxbuf;
    rxbuf.reserve(64 * 1024);
    for (;;) {
        if (!recv_exact(client, rxbuf, kFrameHeaderSize)) {
            return false;
        }
        const auto* hdr = reinterpret_cast<const std::uint8_t*>(rxbuf.constData());
        const std::uint8_t type = hdr[0];
        const std::uint32_t len = read_u32_be(hdr + 1);
        if (len > kMaxFrameLen) {
            // Agent shouldn't send oversized frames; bail rather than
            // chew arbitrary host memory.
            return false;
        }
        const auto frame_total = kFrameHeaderSize + static_cast<qsizetype>(len);
        if (!recv_exact(client, rxbuf, frame_total)) {
            return false;
        }
        if (type == kFrameTypeControl) {
            try {
                const auto j = nlohmann::json::parse(
                    rxbuf.constData() + kFrameHeaderSize,
                    rxbuf.constData() + frame_total);
                if (j.is_object()
                    && j.value("id", std::uint64_t{0}) == kProbeId
                    && j.contains("result")) {
                    return true;
                }
            } catch (const nlohmann::json::exception&) {
                // Malformed control payload — skip and keep draining.
            }
        }
        rxbuf.remove(0, frame_total);
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
    const quint32 cid = cid_;
    const quint32 port = port_;
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        probe_in_flight_ = false;
        const bool reachable = watcher->result();
        watcher->deleteLater();
        if (reachable) {
            const auto line = std::string{"guest agent reachable on cid="}
                              + std::to_string(cid_) + " port=" + std::to_string(port_)
                              + " for VM " + uuid_.toString().toStdString();
            LSW_INFO("{}", line);
            timer_.stop();
            Q_EMIT ready(uuid_);
        }
    });
    watcher->setFuture(QtConcurrent::run([cid, port] { return probe_reachable(cid, port); }));
}

}  // namespace lsw::manager::install
