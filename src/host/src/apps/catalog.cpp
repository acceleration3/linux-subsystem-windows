#include "lsw/host/apps/catalog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::apps {

namespace {
std::optional<QList<AppEntry>> parse_reply(const link::AgentReply& reply) {
    if (!reply.ok) {
        LSW_WARN("list_apps: {}", reply.error);
        return std::nullopt;
    }
    const auto& doc = reply.body;
    if (!doc.is_object() || !doc.contains("apps") || !doc["apps"].is_array()) {
        LSW_WARN("list_apps: the reply has no \"apps\" array");
        return std::nullopt;
    }
    QList<AppEntry> out;
    for (const auto& o : doc["apps"]) {
        if (!o.is_object()) {
            continue;
        }
        AppEntry e;
        e.id = QString::fromStdString(o.value("id", std::string{}));
        e.name = QString::fromStdString(o.value("name", std::string{}));
        e.exe_path = QString::fromStdString(o.value("exe_path", std::string{}));
        e.args = QString::fromStdString(o.value("args", std::string{}));
        if (const auto it = o.find("extensions"); it != o.end() && it->is_array()) {
            for (const auto& extension : *it) {
                if (extension.is_string()) {
                    e.extensions << QString::fromStdString(extension.get<std::string>());
                }
            }
        }
        const auto icon_b64 = o.value("icon_png_base64", std::string{});
        if (!icon_b64.empty()) {
            e.icon.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(icon_b64)),
                                "PNG");
        }
        out.append(std::move(e));
    }
    return out;
}

std::optional<QHash<QString, QStringList>> parse_store_reply(const link::AgentReply& reply) {
    if (!reply.ok || !reply.body.is_object() || !reply.body.contains("apps") ||
        !reply.body["apps"].is_array()) {
        return std::nullopt;
    }
    QHash<QString, QStringList> out;
    for (const auto& o : reply.body["apps"]) {
        QStringList extensions;
        if (const auto it = o.find("extensions"); it != o.end() && it->is_array()) {
            for (const auto& extension : *it) {
                if (extension.is_string()) {
                    extensions << QString::fromStdString(extension.get<std::string>());
                }
            }
        }
        out.insert(QString::fromStdString(o.value("aumid", std::string{})).toLower(), extensions);
    }
    return out;
}

}

AppCatalog::AppCatalog(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] {
        if (online_ && rpc_.has_target()) {
            refresh();
        }
    });
    changed_sub_ = rpc_.subscribe(linkproto::topic::kAppsChanged, [this](const nlohmann::json&) {
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    });
}

constexpr std::chrono::milliseconds kListTimeout{60000};
constexpr int kRetryMs = 15000;

void AppCatalog::set_agent_online(bool online) {
    online_ = online;
    if (!online) {
        retry_.stop();
    }
}

void AppCatalog::refresh() {
    if (refreshing_) {
        again_ = true;
        return;
    }
    refreshing_ = true;
    retry_.stop();
    QtConcurrent::run([&rpc = rpc_] {
        return parse_reply(rpc.request(linkproto::cmd::kListApps, nlohmann::json::object(), kListTimeout));
    }).then(this, [this](std::optional<QList<AppEntry>> entries) {
        refreshing_ = false;
        if (!entries) {
            Q_EMIT failed(QStringLiteral("the agent did not list its applications"));
            if (online_) {
                retry_.start(kRetryMs);
            }
        } else {
            entries_ = std::move(*entries);
            Q_EMIT loaded(entries_);
        }
        if (std::exchange(again_, false)) {
            refresh();
        }
    });
    QtConcurrent::run([&rpc = rpc_] {
        return parse_store_reply(rpc.request(linkproto::cmd::kListStoreApps, {{"icon_size", 16}}, kListTimeout));
    }).then(this, [this](std::optional<QHash<QString, QStringList>> extensions) {
        if (extensions) {
            Q_EMIT store_extensions_loaded(*extensions);
        }
    });
}

}
