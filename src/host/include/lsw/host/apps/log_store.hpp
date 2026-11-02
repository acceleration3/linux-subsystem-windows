#pragma once

#include "lsw/host/apps/log_record.hpp"
#include "lsw/host/link/event_subscription.hpp"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <mutex>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::apps {
class LogStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kCapacity = 20000;
    static constexpr int kBatchMs = 50;

    explicit LogStore(QObject* parent = nullptr);
    ~LogStore() override;

    [[nodiscard]] const QVector<LogRecord>& records() const { return records_; }

    [[nodiscard]] quint64 total() const noexcept { return total_; }

    [[nodiscard]] const QStringList& sources() const noexcept { return sources_; }

    [[nodiscard]] QVector<LogRecord> recent(int count, const QString& source = {}) const;
    [[nodiscard]] static bool source_matches(const QString& record_source, const QString& wanted);

    void attach(link::AgentRpcClient& rpc);

    void post(LogRecord record);

    void clear();

    [[nodiscard]] static LogStore* process_store() noexcept;

Q_SIGNALS:
    void appended(const QVector<lsw::host::apps::LogRecord>& records);

    void cleared();

private:
    Q_INVOKABLE void arm_flush();
    void flush_pending();

    QVector<LogRecord> records_;
    QStringList sources_;
    quint64 total_ = 0;

    std::mutex mutex_;
    QVector<LogRecord> pending_;
    QHash<QString, quint64> last_seq_;
    bool scheduled_ = false;
    qint64 last_flush_ms_ = 0;

    link::EventSubscription log_sub_;
};

}
