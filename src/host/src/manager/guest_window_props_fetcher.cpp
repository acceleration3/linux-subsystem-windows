#include "lsw/host/manager/guest_window_props_fetcher.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QFutureWatcher>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

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
