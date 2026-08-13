#include "lsw/host/manager/guest_window_props_fetcher.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QFutureWatcher>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

// Four-element JSON array -> QRect. Empty when absent or degenerate,
// which is how "this window has no such button" is spelled.
[[nodiscard]] QRect rect_from(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array() || it->size() != 4) {
        return {};
    }
    const int w = it->at(2).get<int>();
    const int h = it->at(3).get<int>();
    if (w <= 0 || h <= 0) {
        return {};
    }
    return QRect(it->at(0).get<int>(), it->at(1).get<int>(), w, h);
}

}  // namespace

GuestWindowPropsFetcher::GuestWindowPropsFetcher(link::AgentRpcClient& rpc,
                                                 std::uint64_t hwnd, QObject* parent)
    : QObject(parent), rpc_(rpc), hwnd_(hwnd) {}

void GuestWindowPropsFetcher::start() {
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
        const QByteArray reply = watcher->result();
        watcher->deleteLater();

        GuestWindowProps props;
        try {
            const auto json = nlohmann::json::parse(reply.constData(),
                                                    reply.constData() + reply.size());
            if (json.is_object() && json.value("valid", false)) {
                props.valid = true;
                props.width = json.value("w", 800);
                props.height = json.value("h", 600);
                props.resizable = json.value("resizable", true);
                props.min_width = json.value("min_w", 0);
                props.min_height = json.value("min_h", 0);
                props.screen_width = json.value("screen_w", 0);
                props.screen_height = json.value("screen_h", 0);
                props.button_minimize = rect_from(json, "btn_min");
                props.button_maximize = rect_from(json, "btn_max");
                props.button_close = rect_from(json, "btn_close");
                props.button_help = rect_from(json, "btn_help");
            }
        } catch (const nlohmann::json::exception&) {
            // Leave props.valid false; the mirror falls back to
            // image-driven sizing rather than never showing.
        }
        Q_EMIT ready(props);
    });
    const std::uint64_t hwnd = hwnd_;
    watcher->setFuture(QtConcurrent::run([this, hwnd] {
        return rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kWindowProps},
                                        {"params", {{"hwnd", hwnd}}}});
    }));
}

}  // namespace lsw::host::manager
