#pragma once

#include "lsw/host/apps/app_entry.hpp"
#include "lsw/host/link/event_subscription.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {
class AppCatalog : public QObject {
    Q_OBJECT
public:
    explicit AppCatalog(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void refresh();

    void set_agent_online(bool online);

    [[nodiscard]] const QList<AppEntry>& entries() const noexcept { return entries_; }

Q_SIGNALS:
    void loaded(const QList<AppEntry>& entries);
    void store_extensions_loaded(const QHash<QString, QStringList>& extensions);
    void failed(const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
    QList<AppEntry> entries_;
    QTimer retry_;
    bool online_ = false;
    bool refreshing_ = false;
    bool again_ = false;
    link::EventSubscription changed_sub_;
};

}
