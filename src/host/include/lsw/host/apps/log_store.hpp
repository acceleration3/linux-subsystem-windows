#pragma once

#include "lsw/host/apps/log_record.hpp"
#include "lsw/host/link/event_subscription.hpp"

#include <QObject>
#include <QVector>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {

// Bounded ring of LogRecords pushed by the agent over the
// bidirectional RPC channel. Holds the current session's logs
// (capped at kCapacity) so multiple LogViewer windows share the same
// backing data and re-opening the viewer doesn't lose history.
//
// ``attach`` subscribes to the agent's ``log`` event — the agent fires
// that at us as records are appended to its own ring, so the manager
// observes near-realtime push (no polling).
class LogStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kCapacity = 5000;

    explicit LogStore(QObject* parent = nullptr);

    [[nodiscard]] const QVector<LogRecord>& records() const { return records_; }

    // Wire ``rpc`` so this store auto-appends from incoming ``log``
    // events. ``rpc`` must outlive this store.
    void attach(link::AgentRpcClient& rpc);

    // Drop everything (called when the user switches VMs).
    void clear();

Q_SIGNALS:
    // Emitted after each appended batch so live LogViewer instances
    // can update incrementally.
    void appended(const QVector<lsw::host::apps::LogRecord>& records);

    // Emitted after ``clear`` so viewers can reset their text.
    void cleared();

private:
    Q_INVOKABLE void on_record(lsw::host::apps::LogRecord rec);

    QVector<LogRecord> records_;
    quint64 max_seq_seen_ = 0;  // dedupe across reconnect storms

    link::EventSubscription log_sub_;
};

}  // namespace lsw::host::apps
