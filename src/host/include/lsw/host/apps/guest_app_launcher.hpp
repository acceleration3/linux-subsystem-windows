#pragma once

#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/link/event_subscription.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {
class WindowMirror;
}

namespace lsw::host::apps {
class GuestAppLauncher : public QObject {
    Q_OBJECT
public:
    explicit GuestAppLauncher(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void launch(const apps::AppConfig& app, const QString& activation_token,
                const QStringList& documents = {});

    void set_output_source(std::function<QString()> source) { output_source_ = std::move(source); }

Q_SIGNALS:
    void launched(const QString& guest_path, quint32 pid);
    void failed(const QString& guest_path, const QString& error, const QStringList& documents);
    void changed();

public:
    [[nodiscard]] bool opening(const apps::AppConfig& app) const;
    [[nodiscard]] bool running(const apps::AppConfig& app) const;

private:
    [[nodiscard]] QString key(const apps::AppConfig& app) const;
    void identified(const QString& identity, const QString& aumid, const QString& path);
    void forget(const QString& identity);
    void launch_ended(quint32 pid);
    link::AgentRpcClient& rpc_;
    std::function<QString()> output_source_;
    QSet<QString> pending_;
    struct Queued {
        apps::AppConfig app;
        QString activation_token;
        QStringList documents;
    };
    void flush_queued(const QString& identity);
    QHash<QString, Queued> queued_;
    QHash<QString, QPointer<mirror::WindowMirror>> mirrors_;
    QHash<QString, quint32> pids_;
    QHash<QString, QString> shown_by_;
    QList<quint32> ended_;
    link::EventSubscription ended_sub_;
};

}
