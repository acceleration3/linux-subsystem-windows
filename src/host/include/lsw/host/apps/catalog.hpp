#pragma once

#include "lsw/host/apps/app_entry.hpp"

#include <QList>
#include <QObject>
#include <QString>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {

// Asks the guest agent for the Start menu app list. Async; emits
// ``loaded`` when done. Caches the last reply.
class AppCatalog : public QObject {
    Q_OBJECT
public:
    explicit AppCatalog(link::AgentRpcClient& rpc, QObject* parent = nullptr);

    void refresh();

    [[nodiscard]] const QList<AppEntry>& entries() const noexcept { return entries_; }

Q_SIGNALS:
    void loaded(const QList<AppEntry>& entries);
    void failed(const QString& error_message);

private:
    link::AgentRpcClient& rpc_;
    QList<AppEntry> entries_;
};

}  // namespace lsw::host::apps
