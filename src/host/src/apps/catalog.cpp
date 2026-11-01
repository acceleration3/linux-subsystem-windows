#include "lsw/host/apps/catalog.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

#include <optional>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::apps {

namespace {

// Nothing for a reply that is not a listing: an empty list would say the
// guest has no applications, and the store drops what the agent stops
// reporting.
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

// The file types of each store application, by lowercase AUMID. Nothing for
// a reply that is not a listing, so that a refused request does not clear
// what is known.
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

}  // namespace

AppCatalog::AppCatalog(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void AppCatalog::refresh() {
    // The worker holds the channel, not ``this``: the catalog may be gone
    // by the time the agent answers, and ``then(this, …)`` drops the
    // continuation if so.
    QtConcurrent::run([&rpc = rpc_] { return parse_reply(rpc.request(linkproto::cmd::kListApps)); })
        .then(this, [this](std::optional<QList<AppEntry>> entries) {
            if (!entries) {
                Q_EMIT failed(QStringLiteral("the agent did not list its applications"));
                return;
            }
            entries_ = std::move(*entries);
            Q_EMIT loaded(entries_);
    // The packaged applications' file types too, which only the user's
    // session knows; the smallest icon, since only the types are wanted.
    QtConcurrent::run([&rpc = rpc_] {
        return parse_store_reply(rpc.request(linkproto::cmd::kListStoreApps, {{"icon_size", 16}}));
    }).then(this, [this](std::optional<QHash<QString, QStringList>> extensions) {
        if (extensions) {
            Q_EMIT store_extensions_loaded(*extensions);
        }
    });
        });
}

}  // namespace lsw::host::apps
