#include "lsw/host/link/guest_window_model.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QFutureWatcher>
#include <QMetaObject>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

namespace lsw::host::link {

namespace {

[[nodiscard]] QList<GuestWindow> parse_windows(const QByteArray& reply) {
    QList<GuestWindow> out;
    if (reply.isEmpty()) {
        return out;
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                             reply.constData() + reply.size());
        if (!j.contains("windows") || !j["windows"].is_array()) {
            return out;
        }
        out.reserve(static_cast<qsizetype>(j["windows"].size()));
        for (const auto& w : j["windows"]) {
            GuestWindow gw;
            gw.hwnd = w.value("hwnd", std::uint64_t{0});
            gw.owner = w.value("owner", std::uint64_t{0});
            gw.pid = static_cast<quint32>(w.value("pid", std::uint64_t{0}));
            gw.root_pid = static_cast<quint32>(w.value("root_pid", std::uint64_t{0}));
            gw.title = QString::fromStdString(w.value("title", std::string{}));
            gw.exe_path = QString::fromStdString(w.value("exe_path", std::string{}));
            gw.screen_x = w.value("screen_x", 0);
            gw.screen_y = w.value("screen_y", 0);
            gw.minimized = w.value("minimized", false);
            gw.maximized = w.value("maximized", false);
            gw.has_caption = w.value("has_caption", false);
            out.append(gw);
        }
    } catch (const nlohmann::json::exception&) {
    }
    return out;
}

}  // namespace

GuestWindowModel::GuestWindowModel(AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {
    qRegisterMetaType<QList<GuestWindow>>("QList<lsw::host::link::GuestWindow>");
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &GuestWindowModel::poll);

    // One registration for the whole process. Handlers run on the reader
    // thread, so hop to the GUI thread before touching poll state.
    auto kick = [this](const nlohmann::json&) {
        QMetaObject::invokeMethod(this, &GuestWindowModel::refresh_now,
                                  Qt::QueuedConnection);
    };
    opened_token_ = rpc_.on_event(QString::fromLatin1(linkproto::topic::kUiaWindowOpened), kick);
    closed_token_ = rpc_.on_event(QString::fromLatin1(linkproto::topic::kUiaWindowClosed), kick);
}

GuestWindowModel::~GuestWindowModel() {
    rpc_.off_event(opened_token_);
    rpc_.off_event(closed_token_);
}

void GuestWindowModel::subscribe() {
    if (++subscribers_ == 1) {
        timer_.start();
        refresh_now();
    }
}

void GuestWindowModel::unsubscribe() {
    if (subscribers_ == 0) {
        return;
    }
    --subscribers_;
    if (subscribers_ == 0) {
        timer_.stop();
    }
}

void GuestWindowModel::refresh_now() {
    if (subscribers_ > 0) {
        poll();
    }
}

void GuestWindowModel::poll() {
    // One enumeration in flight at a time: a slow reply must not queue
    // up a backlog of identical questions.
    if (poll_in_flight_) {
        return;
    }
    poll_in_flight_ = true;

    auto* watcher = new QFutureWatcher<QList<GuestWindow>>(this);
    connect(watcher, &QFutureWatcher<QList<GuestWindow>>::finished, this,
            [this, watcher] {
                poll_in_flight_ = false;
                latest_ = watcher->result();
                watcher->deleteLater();
                Q_EMIT updated(latest_);
            });
    watcher->setFuture(QtConcurrent::run([this] {
        return parse_windows(
            rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kListWindows}}));
    }));
}

}  // namespace lsw::host::link
