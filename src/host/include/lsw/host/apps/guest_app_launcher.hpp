#pragma once

#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/link/event_subscription.hpp"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::mirror {
class GuestMirrors;
}

namespace lsw::host::apps {
class GuestAppLauncher : public QObject {
    Q_OBJECT
public:
    GuestAppLauncher(link::AgentRpcClient& rpc, mirror::GuestMirrors& mirrors, QObject* parent = nullptr);

    void launch(const apps::AppConfig& app, const QString& activation_token,
                const QStringList& documents = {});

Q_SIGNALS:
    void launched(const QString& guest_path, quint32 pid);
    void failed(const QString& guest_path, const QString& error, const QStringList& documents);
    void changed();

public:
    [[nodiscard]] bool opening(const apps::AppConfig& app) const;
    [[nodiscard]] bool running(const apps::AppConfig& app) const;

private:
    [[nodiscard]] QString key(const apps::AppConfig& app) const;
    void launch_ended(quint32 pid, quint64 created);
    void shown(const QString& identity);
    void mirror_failed(const QString& identity, const QString& error);
    void mirror_closed(const QString& identity);
    void merged(const QString& identity, const QString& into);
    link::AgentRpcClient& rpc_;
    mirror::GuestMirrors& mirrors_;
    QSet<QString> pending_;
    struct Queued {
        apps::AppConfig app;
        QString activation_token;
        QStringList documents;
    };
    void flush_queued(const QString& identity);
    QHash<QString, Queued> queued_;
    struct Launched {
        QString path;
        QStringList documents;
    };
    QHash<QString, Launched> launched_;
    QList<QPair<quint32, quint64>> ended_;
    link::EventSubscription ended_sub_;
};

}
