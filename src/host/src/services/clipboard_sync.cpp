#include "lsw/host/services/clipboard_sync.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/platform/data_control_clipboard.hpp"
#include "lsw/host/platform/qt_clipboard.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QGuiApplication>
#include <QMetaObject>
#include <QTimer>
#include <nlohmann/json.hpp>

namespace lsw::host::services {

namespace {
using platform::ClipboardPayload;
using platform::HostClipboard;

QByteArray decode(const nlohmann::json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        return {};
    }
    return QByteArray::fromBase64(QByteArray::fromStdString(it->get<std::string>()));
}
}

ClipboardSync::ClipboardSync(link::AgentRpcClient& rpc, StatusHub& status,
                             config::ManagerSettings& settings, QObject* parent)
    : QObject(parent), rpc_(rpc), status_(status), settings_(settings) {
    sub_ = rpc_.subscribe(linkproto::topic::kClipboardChanged, [this](const nlohmann::json& f) {
        const bool text = f.value("text", false);
        const bool image = f.value("image", false);
        std::optional<quint32> session;
        if (f.contains("session") && f["session"].is_number_unsigned()) {
            session = f["session"].get<quint32>();
        }
        QMetaObject::invokeMethod(
            this, [this, text, image, session] { on_guest_changed(text, image, session); },
            Qt::QueuedConnection);
    });
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == config::ManagerSettings::Key::kShareClipboard) {
            apply_setting();
        }
    });
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm) {
            on_status();
        }
    });
}

ClipboardSync::~ClipboardSync() {
    sub_.reset();
}

void ClipboardSync::start() {
    started_ = true;
    apply_setting();
}

void ClipboardSync::apply_setting() {
    if (!started_) {
        return;
    }
    const bool on = settings_.bool_value(config::ManagerSettings::Key::kShareClipboard);
    if (on == enabled_) {
        return;
    }
    enabled_ = on;
    if (on) {
        online_ = status_.vm().agent_online();
        create_backend();
    } else {
        stop();
    }
    publish_status();
}

void ClipboardSync::create_backend() {
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        use_qt_backend();
    } else {
        auto wayland = std::make_unique<platform::DataControlClipboard>();
        connect(wayland.get(), &platform::DataControlClipboard::unsupported, this, [this] {
            LSW_DEBUG("clipboard: no data control, using the Qt clipboard");
            if (clipboard_) {
                clipboard_.release()->deleteLater();
            }
            use_qt_backend();
            publish_status();
        });
        wire(*wayland);
        wayland->start();
        clipboard_ = std::move(wayland);
    }
    LSW_DEBUG("clipboard: sharing on");
}

void ClipboardSync::use_qt_backend() {
    auto qt = std::make_unique<platform::QtClipboard>();
    wire(*qt);
    clipboard_ = std::move(qt);
}

void ClipboardSync::wire(platform::HostClipboard& backend) {
    connect(&backend, &HostClipboard::changed, this,
            [this](const ClipboardPayload& payload) { on_host_changed(payload); });
    connect(&backend, &HostClipboard::cleared, this, [this] { on_host_cleared(); });
    connect(&backend, &HostClipboard::state_changed, this, [this] { publish_status(); });
}

void ClipboardSync::stop() {
    clipboard_.reset();
    host_current_ = {};
    pending_push_.reset();
    guest_dirty_ = false;
    ++guest_gen_;
    LSW_DEBUG("clipboard: sharing off");
}

void ClipboardSync::publish_status() {
    ClipboardStatus status;
    status.enabled = enabled_;
    if (enabled_ && clipboard_) {
        status.working = clipboard_->working();
        status.limited = clipboard_->limited();
    }
    status_.set_clipboard(status);
}

void ClipboardSync::on_status() {
    const bool online = status_.vm().agent_online();
    const bool arrived = online && !online_;
    online_ = online;
    if (enabled_ && arrived && host_undelivered_ && !host_current_.empty()) {
        queue_push(Push{host_current_});
    }
}

void ClipboardSync::on_guest_changed(bool text, bool image, std::optional<quint32> session) {
    if (!enabled_ || !clipboard_) {
        return;
    }
    ++guest_gen_;
    guest_session_ = session;
    if (!text && !image) {
        guest_dirty_ = false;
        host_current_ = {};
        host_undelivered_ = false;
        clipboard_->clear_if_owned();
        return;
    }
    guest_dirty_ = true;
    pump_guest();
}

void ClipboardSync::pump_guest() {
    if (guest_in_flight_ || !guest_dirty_) {
        return;
    }
    guest_dirty_ = false;
    guest_in_flight_ = true;
    const quint64 gen = guest_gen_;
    nlohmann::json params = nlohmann::json::object();
    if (guest_session_) {
        params["session"] = *guest_session_;
    }
    rpc_.request_async(linkproto::cmd::kClipboardGet, std::move(params), this,
                       [this, gen](const link::AgentReply& reply) {
                           guest_in_flight_ = false;
                           if (enabled_ && gen == guest_gen_) {
                               if (reply.ok) {
                                   on_guest_content(reply.body);
                               } else {
                                   LSW_DEBUG("clipboard: guest read failed: {}", reply.error);
                                   retry_guest(gen);
                               }
                           }
                           pump_guest();
                       });
}

void ClipboardSync::retry_guest(quint64 gen) {
    if (guest_retried_ == gen) {
        return;
    }
    guest_retried_ = gen;
    QTimer::singleShot(300, this, [this, gen] {
        if (enabled_ && gen == guest_gen_) {
            guest_dirty_ = true;
            pump_guest();
        }
    });
}

void ClipboardSync::on_guest_content(const nlohmann::json& body) {
    ClipboardPayload payload;
    if (body.contains("text") && body["text"].is_string()) {
        payload.text = QString::fromStdString(body["text"].get<std::string>());
    }
    payload.png = decode(body, "png");
    payload.secret = body.value("secret", false);
    if (payload.empty() || !clipboard_) {
        return;
    }
    LSW_DEBUG("clipboard: guest to host text={} png={}B secret={}", payload.text.has_value(),
              payload.png.size(), payload.secret);
    host_current_ = {};
    host_undelivered_ = false;
    clipboard_->set(payload);
}

void ClipboardSync::on_host_changed(const ClipboardPayload& payload) {
    if (!enabled_ || payload.empty()) {
        return;
    }
    ++guest_gen_;
    guest_dirty_ = false;
    host_current_ = payload.secret ? ClipboardPayload{} : payload;
    host_undelivered_ = !online_ && !payload.secret;
    if (online_) {
        queue_push(Push{payload});
    }
}

void ClipboardSync::on_host_cleared() {
    if (!enabled_) {
        return;
    }
    ++guest_gen_;
    guest_dirty_ = false;
    host_current_ = {};
    host_undelivered_ = false;
    if (online_) {
        queue_push(Push{});
    }
}

void ClipboardSync::queue_push(Push push) {
    pending_push_ = std::move(push);
    pump_push();
}

void ClipboardSync::pump_push() {
    if (push_in_flight_ || !pending_push_) {
        return;
    }
    const Push push = std::move(*pending_push_);
    pending_push_.reset();
    nlohmann::json params = nlohmann::json::object();
    if (!push.payload) {
        params["clear"] = true;
    } else {
        const ClipboardPayload& payload = *push.payload;
        if (payload.text) {
            const QByteArray utf8 = payload.text->toUtf8();
            if (utf8.size() <= HostClipboard::kMaxTextBytes) {
                params["text"] = utf8.toStdString();
            } else {
                LSW_DEBUG("clipboard: host text of {} bytes dropped, over the cap", utf8.size());
            }
        }
        if (!payload.png.isEmpty()) {
            if (payload.png.size() <= HostClipboard::kMaxImageBytes) {
                params["png"] = payload.png.toBase64().toStdString();
            } else {
                LSW_DEBUG("clipboard: host image of {} bytes dropped, over the cap",
                          payload.png.size());
            }
        }
        if (static_cast<qsizetype>(params.dump().size()) > HostClipboard::kMaxMessageChars &&
            params.contains("png")) {
            LSW_DEBUG("clipboard: host image of {} bytes dropped, message over budget",
                      payload.png.size());
            params.erase("png");
        }
        if (static_cast<qsizetype>(params.dump().size()) > HostClipboard::kMaxMessageChars) {
            LSW_DEBUG("clipboard: host text dropped, message over budget");
            params.erase("text");
        }
        if (payload.secret) {
            params["secret"] = true;
        }
        if (!params.contains("text") && !params.contains("png")) {
            return;
        }
        LSW_DEBUG("clipboard: host to guest text={} png={}B secret={}", params.contains("text"),
                  payload.png.size(), payload.secret);
    }
    push_in_flight_ = true;
    rpc_.request_async(linkproto::cmd::kClipboardSet, std::move(params), this,
                       [this, content = push.payload.has_value()](const link::AgentReply& reply) {
                           push_in_flight_ = false;
                           if (!reply.ok) {
                               LSW_DEBUG("clipboard: host push failed: {}", reply.error);
                               host_undelivered_ = content && !host_current_.empty();
                           } else if (!pending_push_) {
                               host_undelivered_ = false;
                           }
                           pump_push();
                       });
}

}
