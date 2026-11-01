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

// Asks the guest agent for the Start menu app list. Async; emits
// ``loaded`` when done, and ``failed`` - never ``loaded`` with an empty
// list - when the agent could not say, so that nothing is taken for
// "the guest has no applications". Caches the last reply, and asks again
// by itself when the agent says the Start menu changed.
class AppCatalog : public QObject {
    Q_OBJECT
public:
    explicit AppCatalog(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void refresh();

    // Whether the agent answers. A read that fails while it does (no user
    // is logged on yet) is tried again in a while, and not while it does not.
    void set_agent_online(bool online);

    [[nodiscard]] const QList<AppEntry>& entries() const noexcept { return entries_; }

Q_SIGNALS:
    void loaded(const QList<AppEntry>& entries);
    // The file types of the packaged applications, by lowercase AUMID.
    void store_extensions_loaded(const QHash<QString, QStringList>& extensions);
    void failed(const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
    QList<AppEntry> entries_;
    QTimer retry_;
    bool online_ = false;
    bool refreshing_ = false;
    bool again_ = false;
    // Declared last: unregisters the handler before the rest is gone.
    link::EventSubscription changed_sub_;
};

}  // namespace lsw::host::apps
