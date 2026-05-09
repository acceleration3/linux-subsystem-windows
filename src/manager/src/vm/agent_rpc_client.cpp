#include "lsw/manager/vm/agent_rpc_client.hpp"

#include "lsw/log.hpp"

#include <QMetaObject>

#include <chrono>
#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

namespace lsw::manager::vm {

namespace {

constexpr int kRecvChunk = 64 * 1024;
constexpr int kReconnectBackoffMs = 500;

// Wire format — mirror of the agent's host_endpoint.cpp comment.
//   [type:u8][len:u32-BE][payload:len bytes]
// type=1 control (JSON payload)
// type=2 frame   ([hwnd:u64-BE][png bytes:rest])
// type=3 frame_gone ([hwnd:u64-BE])
constexpr std::uint8_t kFrameTypeControl = 1;
constexpr std::uint8_t kFrameTypeFrame = 2;
constexpr std::uint8_t kFrameTypeFrameGone = 3;
constexpr std::size_t  kFrameHeaderSize = 5;

void write_u32_be(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(v & 0xFFu);
}
std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         | static_cast<std::uint32_t>(p[3]);
}
std::uint64_t read_u64_be(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return v;
}

}  // namespace

AgentRpcClient::AgentRpcClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::uint64_t>("std::uint64_t");
    qRegisterMetaType<QImage>("QImage");
    reader_ = std::thread{[this] { reader_loop(); }};
}

AgentRpcClient::~AgentRpcClient() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(io_mu_);
        drop_connection_locked();
    }
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        for (auto* p : pending_) {
            std::lock_guard<std::mutex> plock(p->mu);
            p->done = true;
            p->reply.clear();
            p->cv.notify_all();
        }
        pending_.clear();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

void AgentRpcClient::set_target(quint32 cid, quint32 port) {
    // Idempotent: if the target hasn't actually changed, don't bump
    // the generation. The wizard's SetupPage and the manager's
    // VmDetailPanel both call set_target with the same (cid, port) at
    // handoff time; bumping every call forced a reconnect that raced
    // with the reader thread (which would observe EOF on the old fd
    // and then close the freshly-opened replacement under io_mu_,
    // leaving the in-flight call() to wait on a dead connection).
    if (cid_.load() == cid && port_.load() == port) {
        return;
    }
    cid_.store(cid);
    port_.store(port);
    generation_.fetch_add(1);
}

void AgentRpcClient::clear_target() {
    set_target(0, 0);
}

AgentRpcClient::EventToken AgentRpcClient::on_event(const QString& topic,
                                                     EventHandler handler) {
    const EventToken token = next_event_token_.fetch_add(1);
    std::lock_guard<std::mutex> lock(handlers_mu_);
    handlers_[topic].append(HandlerEntry{token, std::move(handler)});
    return token;
}

void AgentRpcClient::off_event(EventToken token) {
    if (token == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(handlers_mu_);
    for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
        auto& list = it.value();
        for (qsizetype i = 0; i < list.size(); ++i) {
            if (list[i].token == token) {
                list.removeAt(i);
                return;
            }
        }
    }
}

void AgentRpcClient::drop_connection_locked() {
    client_.close();
    // Wake any in-flight callers so they don't hang forever waiting
    // for a reply that can no longer arrive on this socket. Without
    // this, a connect attempt that *succeeded* into a half-open
    // transport (e.g. qemu hostfwd accepted the host connect but the
    // guest agent wasn't actually listening yet during VM boot)
    // leaves the call()'s pending.cv.wait stranded → kick_agent_poll's
    // ``in_flight`` flag stays true → no further polls are ever
    // attempted → the manager never detects the agent coming up
    // until it's restarted. The reader's reconnect loop is fine; the
    // pending caller just needs to be released so the next monitor
    // tick can try again.
    std::lock_guard<std::mutex> lock(pending_mu_);
    for (auto* p : pending_) {
        std::lock_guard<std::mutex> plock(p->mu);
        p->done = true;
        p->reply.clear();
        p->cv.notify_all();
    }
}

bool AgentRpcClient::ensure_connected_locked() {
    const auto gen = generation_.load();
    if (gen != last_generation_) {
        drop_connection_locked();
        last_generation_ = gen;
    }
    if (client_.is_open()) {
        return true;
    }
    const auto cid = cid_.load();
    const auto port = port_.load();
    if (cid == 0 || port == 0) {
        return false;
    }
    if (client_.connect(cid, port) != 0) {
        return false;
    }
    replay_subscriptions_locked();
    return true;
}

// Send one envelope frame on the wire. ``payload`` is the raw bytes
// after the 5-byte header. Caller holds io_mu_.
bool AgentRpcClient::send_locked(const QByteArray& body) {
    // Legacy entry point — wraps ``body`` as a control (type=1)
    // envelope. Used by the previous JSON-line path.
    if (!client_.is_open()) return false;
    std::uint8_t header[kFrameHeaderSize];
    header[0] = kFrameTypeControl;
    write_u32_be(header + 1, static_cast<std::uint32_t>(body.size()));
    QByteArray framed;
    framed.reserve(static_cast<qsizetype>(kFrameHeaderSize) + body.size());
    framed.append(reinterpret_cast<const char*>(header),
                   static_cast<qsizetype>(kFrameHeaderSize));
    framed.append(body);
    return client_.send_all(framed) == 0;
}

void AgentRpcClient::replay_subscriptions_locked() {
    QSet<std::uint64_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(subs_mu_);
        snapshot = subscribed_windows_;
    }
    for (const auto hwnd : snapshot) {
        nlohmann::json req = {
            {"id", next_id_.fetch_add(1)},
            {"cmd", "subscribe_window"},
            {"params", {{"hwnd", hwnd}}},
        };
        const std::string s = req.dump();
        if (!send_locked(QByteArray(s.data(), static_cast<qsizetype>(s.size())))) {
            return;
        }
    }
}

void AgentRpcClient::subscribe_window(std::uint64_t hwnd) {
    {
        std::lock_guard<std::mutex> lock(subs_mu_);
        subscribed_windows_.insert(hwnd);
    }
    std::lock_guard<std::mutex> lock(io_mu_);
    if (!ensure_connected_locked()) return;
    nlohmann::json req = {
        {"id", next_id_.fetch_add(1)},
        {"cmd", "subscribe_window"},
        {"params", {{"hwnd", hwnd}}},
    };
    const std::string s = req.dump();
    (void)send_locked(QByteArray(s.data(), static_cast<qsizetype>(s.size())));
}

void AgentRpcClient::unsubscribe_window(std::uint64_t hwnd) {
    {
        std::lock_guard<std::mutex> lock(subs_mu_);
        subscribed_windows_.remove(hwnd);
    }
    std::lock_guard<std::mutex> lock(io_mu_);
    if (!ensure_connected_locked()) return;
    nlohmann::json req = {
        {"id", next_id_.fetch_add(1)},
        {"cmd", "unsubscribe_window"},
        {"params", {{"hwnd", hwnd}}},
    };
    const std::string s = req.dump();
    (void)send_locked(QByteArray(s.data(), static_cast<qsizetype>(s.size())));
}

void AgentRpcClient::post_oneway(const nlohmann::json& request) {
    if (stop_.load()) {
        return;
    }
    nlohmann::json req = request;
    req["id"] = next_id_.fetch_add(1);
    const std::string body = req.dump();
    const QByteArray bytes(body.data(), static_cast<qsizetype>(body.size()));
    std::lock_guard<std::mutex> lock(io_mu_);
    if (!ensure_connected_locked()) {
        return;  // drop — input is loss-tolerant
    }
    (void)send_locked(bytes);
}

QByteArray AgentRpcClient::call(const QByteArray& request_line) {
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(request_line.constData(),
                                     request_line.constData() + request_line.size());
    } catch (const nlohmann::json::exception&) {
        LSW_WARN("agent_rpc_client: caller passed malformed JSON to call()");
        return {};
    }
    return call(req);
}

QByteArray AgentRpcClient::call(const nlohmann::json& request) {
    if (stop_.load()) return {};
    const auto id = next_id_.fetch_add(1);
    nlohmann::json req = request;
    req["id"] = id;
    const std::string body = req.dump();
    const QByteArray bytes(body.data(), static_cast<qsizetype>(body.size()));

    Pending pending;
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        pending_.insert(id, &pending);
    }
    struct Eraser {
        AgentRpcClient* self;
        quint64 id;
        ~Eraser() {
            std::lock_guard<std::mutex> lock(self->pending_mu_);
            self->pending_.remove(id);
        }
    } eraser{this, id};

    {
        std::lock_guard<std::mutex> lock(io_mu_);
        if (!ensure_connected_locked()) {
            return {};
        }
        if (!send_locked(bytes)) {
            drop_connection_locked();
            if (!ensure_connected_locked() || !send_locked(bytes)) {
                return {};
            }
        }
    }

    std::unique_lock<std::mutex> plock(pending.mu);
    pending.cv.wait(plock, [&] { return pending.done; });
    return std::move(pending.reply);
}

void AgentRpcClient::deliver_reply(quint64 id, QByteArray body) {
    Pending* p = nullptr;
    {
        std::lock_guard<std::mutex> lock(pending_mu_);
        const auto it = pending_.constFind(id);
        if (it == pending_.constEnd()) return;
        p = it.value();
    }
    std::lock_guard<std::mutex> plock(p->mu);
    p->reply = std::move(body);
    p->done = true;
    p->cv.notify_all();
}

void AgentRpcClient::dispatch_event(const nlohmann::json& frame) {
    if (!frame.contains("event") || !frame["event"].is_string()) return;
    const auto topic = QString::fromStdString(frame["event"].get<std::string>());
    // Snapshot the handler list under the lock so we don't hold
    // handlers_mu_ while invoking — a handler that touches the rpc
    // (e.g. off_event from its own callback) would deadlock.
    QList<EventHandler> snapshot;
    {
        std::lock_guard<std::mutex> lock(handlers_mu_);
        const auto it = handlers_.constFind(topic);
        if (it == handlers_.constEnd()) return;
        snapshot.reserve(it.value().size());
        for (const auto& entry : it.value()) {
            snapshot.append(entry.handler);
        }
    }
    for (const auto& h : snapshot) {
        h(frame);
    }
}

void AgentRpcClient::reader_loop() {
    std::vector<std::uint8_t> rxbuf;
    rxbuf.reserve(64 * 1024);

    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(io_mu_);
            if (!ensure_connected_locked()) {
                lock.unlock();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kReconnectBackoffMs));
                continue;
            }
        }
        const auto chunk = client_.recv_some(kRecvChunk);
        if (chunk.isEmpty()) {
            std::lock_guard<std::mutex> lock(io_mu_);
            drop_connection_locked();
            rxbuf.clear();
            continue;
        }
        rxbuf.insert(rxbuf.end(),
                     reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                     reinterpret_cast<const std::uint8_t*>(chunk.constData())
                         + chunk.size());

        // Drain complete envelopes.
        std::size_t pos = 0;
        for (;;) {
            if (rxbuf.size() - pos < kFrameHeaderSize) break;
            const std::uint8_t type = rxbuf[pos];
            const std::uint32_t len = read_u32_be(rxbuf.data() + pos + 1);
            if (rxbuf.size() - pos < kFrameHeaderSize + len) break;
            const std::uint8_t* payload = rxbuf.data() + pos + kFrameHeaderSize;

            if (type == kFrameTypeControl) {
                nlohmann::json frame;
                try {
                    frame = nlohmann::json::parse(payload, payload + len);
                } catch (const nlohmann::json::exception& e) {
                    LSW_WARN("agent_rpc_client: malformed control payload: {}",
                             e.what());
                    pos += kFrameHeaderSize + len;
                    continue;
                }
                if (frame.is_object()) {
                    if (frame.contains("event")) {
                        dispatch_event(frame);
                    } else if (frame.contains("id")) {
                        const auto id = frame["id"].get<std::uint64_t>();
                        if (frame.contains("cmd")) {
                            // Agent-initiated request — none defined; drop.
                        } else {
                            QByteArray body;
                            if (frame.contains("result")) {
                                const auto s = frame["result"].dump();
                                body = QByteArray(s.data(),
                                                    static_cast<qsizetype>(s.size()));
                            } else if (frame.contains("error")) {
                                // Propagate the agent's error string as
                                // a parseable JSON body so callers can
                                // show "agent: <msg>" instead of the
                                // empty "no reply" fallback.
                                const auto err_str = frame["error"].get<std::string>();
                                LSW_WARN("agent_rpc_client: id={} error: {}", id, err_str);
                                nlohmann::json err_body = {{"error", err_str}};
                                const auto s = err_body.dump();
                                body = QByteArray(s.data(),
                                                    static_cast<qsizetype>(s.size()));
                            }
                            deliver_reply(id, std::move(body));
                        }
                    }
                }
            } else if (type == kFrameTypeFrame && len >= 8) {
                const std::uint64_t hwnd = read_u64_be(payload);
                // Raw PNG follows the 8-byte hwnd. QImage decodes it
                // directly — no base64, no JSON.
                QImage img;
                img.loadFromData(
                    reinterpret_cast<const uchar*>(payload + 8),
                    static_cast<int>(len - 8), "PNG");
                if (!img.isNull()) {
                    QMetaObject::invokeMethod(
                        this, [this, hwnd, img] { Q_EMIT frame_received(hwnd, img); },
                        Qt::QueuedConnection);
                }
            } else if (type == kFrameTypeFrameGone && len >= 8) {
                const std::uint64_t hwnd = read_u64_be(payload);
                QMetaObject::invokeMethod(
                    this, [this, hwnd] { Q_EMIT window_gone(hwnd); },
                    Qt::QueuedConnection);
            } else {
                LSW_WARN("agent_rpc_client: unknown frame type={} len={}",
                         static_cast<int>(type), len);
            }
            pos += kFrameHeaderSize + len;
        }
        if (pos > 0) {
            rxbuf.erase(rxbuf.begin(),
                         rxbuf.begin() + static_cast<std::ptrdiff_t>(pos));
        }
    }
}

}  // namespace lsw::manager::vm
