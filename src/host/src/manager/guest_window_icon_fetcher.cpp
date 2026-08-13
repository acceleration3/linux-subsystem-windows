#include "lsw/host/manager/guest_window_icon_fetcher.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QFutureWatcher>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

GuestWindowIconFetcher::GuestWindowIconFetcher(link::AgentRpcClient& rpc,
                                               std::uint64_t hwnd, QObject* parent)
    : QObject(parent), rpc_(rpc), hwnd_(hwnd) {}

void GuestWindowIconFetcher::start() {
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
        const QByteArray reply = watcher->result();
        watcher->deleteLater();

        QByteArray png;
        try {
            const auto json = nlohmann::json::parse(reply.constData(),
                                                    reply.constData() + reply.size());
            const auto base64 = json.value("icon_png_base64", std::string{});
            if (!base64.empty()) {
                png = QByteArray::fromBase64(QByteArray::fromStdString(base64));
            }
        } catch (const nlohmann::json::exception&) {
            // An icon we could not read must not block the show.
        }
        Q_EMIT ready(png);
    });
    const std::uint64_t hwnd = hwnd_;
    watcher->setFuture(QtConcurrent::run([this, hwnd] {
        return rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kGetWindowIcon},
                                        {"params", {{"hwnd", hwnd}}}});
    }));
}

}  // namespace lsw::host::manager
