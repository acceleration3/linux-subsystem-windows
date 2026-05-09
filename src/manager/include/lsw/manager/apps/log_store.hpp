#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace lsw::manager::vm { class AgentRpcClient; }

namespace lsw::manager::apps {

// One log record as the manager sees it. Mirrors the agent's wire
// shape: {seq, ts_ms, level, msg}. ``level`` is the agent's string
// label ("INFO", "WARN", ...).
struct LogRecord {
    quint64 seq = 0;
    quint64 ts_ms = 0;
    QString level;
    QString msg;
};

}  // namespace lsw::manager::apps

Q_DECLARE_METATYPE(lsw::manager::apps::LogRecord)
Q_DECLARE_METATYPE(QVector<lsw::manager::apps::LogRecord>)

namespace lsw::manager::apps {

// Bounded ring of LogRecords pushed by the agent over the
// bidirectional RPC channel. Holds the current session's logs
// (capped at kCapacity) so multiple LogViewer windows can share the
// same backing data and re-opening the viewer doesn't lose history.
//
// ``attach`` registers a ``log_record`` handler on the AgentRpcClient
// — the agent fires that RPC at us as records are appended to its
// own ring, so the manager observes near-realtime push (no polling).
//
// Cleared on ``clear`` (called by the owner when switching VMs).
class LogStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kCapacity = 5000;

    explicit LogStore(QObject* parent = nullptr);

    [[nodiscard]] const QVector<LogRecord>& records() const { return records_; }

    // Wire ``rpc`` so this store auto-appends from incoming
    // ``log_record`` requests. Caller keeps ownership of the rpc.
    void attach(vm::AgentRpcClient& rpc);

    // Drop everything (called when the user switches VMs).
    void clear();

Q_SIGNALS:
    // Emitted after each appended batch so live LogViewer instances
    // can update incrementally.
    void appended(const QVector<lsw::manager::apps::LogRecord>& records);

    // Emitted after ``clear`` so viewers can reset their text.
    void cleared();

private:
    Q_INVOKABLE void on_record(lsw::manager::apps::LogRecord rec);

    QVector<LogRecord> records_;
    quint64 max_seq_seen_ = 0;  // dedupe across reconnect storms
};

}  // namespace lsw::manager::apps
