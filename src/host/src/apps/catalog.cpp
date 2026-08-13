#include "lsw/host/apps/catalog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

namespace lsw::host::apps {

namespace {

QList<AppEntry> parse_reply(const QByteArray& payload) {
    QList<AppEntry> out;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(payload.constData(),
                                     payload.constData() + payload.size());
    } catch (const nlohmann::json::exception& e) {
        LSW_WARN("agent reply is not JSON: {}", e.what());
        return out;
    }
    if (!doc.is_object() || !doc.contains("apps") || !doc["apps"].is_array()) {
        return out;
    }
    for (const auto& o : doc["apps"]) {
        if (!o.is_object()) {
            continue;
        }
        AppEntry e;
        e.id = QString::fromStdString(o.value("id", std::string{}));
        e.name = QString::fromStdString(o.value("name", std::string{}));
        e.exe_path = QString::fromStdString(o.value("exe_path", std::string{}));
        const auto icon_b64 = o.value("icon_png_base64", std::string{});
        if (!icon_b64.empty()) {
            e.icon.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(icon_b64)),
                                "PNG");
        }
        out.append(std::move(e));
    }
    return out;
}

}  // namespace

AppCatalog::AppCatalog(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void AppCatalog::refresh() {
    auto* future_watcher = new QFutureWatcher<QList<AppEntry>>(this);
    connect(future_watcher, &QFutureWatcher<QList<AppEntry>>::finished, this,
            [this, future_watcher]() {
                entries_ = future_watcher->result();
                Q_EMIT loaded(entries_);
                future_watcher->deleteLater();
            });
    auto fut = QtConcurrent::run([this]() -> QList<AppEntry> {
        const auto reply = rpc_.call(QByteArrayLiteral("{\"cmd\":\"list_apps\"}"));
        return parse_reply(reply);
    });
    future_watcher->setFuture(fut);
}

}  // namespace lsw::host::apps
