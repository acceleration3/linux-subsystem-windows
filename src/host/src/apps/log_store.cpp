#include "lsw/host/apps/log_store.hpp"

#include "lsw/base/log_format.hpp"
#include "lsw/base/log_tail.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QTimer>

#include <atomic>
#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::apps {

namespace {

std::atomic<LogStore*> g_process_store{nullptr};

}

LogStore::LogStore(QObject* parent) : QObject(parent) {
    qRegisterMetaType<LogRecord>("lsw::host::apps::LogRecord");
    qRegisterMetaType<QVector<LogRecord>>("QVector<lsw::host::apps::LogRecord>");
    LogStore* expected = nullptr;
    g_process_store.compare_exchange_strong(expected, this);
}

LogStore::~LogStore() {
    LogStore* self = this;
    g_process_store.compare_exchange_strong(self, nullptr);
}

LogStore* LogStore::process_store() noexcept {
    return g_process_store.load();
}

void LogStore::attach(link::AgentRpcClient& rpc) {
    log_sub_ = rpc.subscribe(linkproto::topic::kLog, [this](const nlohmann::json& frame) {
        LogRecord rec;
        rec.seq = frame.value("seq", std::uint64_t{0});
        rec.ts_ms = frame.value("ts_ms", std::uint64_t{0});
        rec.level = QString::fromStdString(frame.value("level", std::string{})).toUpper();
        rec.source = QString::fromStdString(frame.value("source", std::string{"agent"}));
        rec.msg = QString::fromStdString(frame.value("msg", std::string{}));
        post(std::move(rec));
    });
}

bool LogStore::source_matches(const QString& record_source, const QString& wanted) {
    if (wanted.isEmpty()) {
        return true;
    }
    if (record_source.compare(wanted, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return record_source.size() > wanted.size() && record_source[wanted.size()] == QLatin1Char(' ')
           && record_source.startsWith(wanted, Qt::CaseInsensitive);
}

QVector<LogRecord> LogStore::recent(int count, const QString& source) const {
    QVector<LogRecord> out;
    for (qsizetype i = records_.size() - 1; i >= 0 && out.size() < count; --i) {
        if (source_matches(records_[i].source, source)) {
            out.push_back(records_[i]);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void LogStore::clear() {
    {
        const std::lock_guard lock{mutex_};
        pending_.clear();
        last_seq_.clear();
    }
    records_.clear();
    sources_.clear();
    Q_EMIT cleared();
}

void LogStore::post(LogRecord rec) {
    const std::lock_guard lock{mutex_};
    if (rec.seq != 0) {
        quint64& last = last_seq_[rec.source];
        if (rec.seq <= last && !(rec.seq == 1 && last > 1)) {
            return;
        }
        last = rec.seq;
    }
    if (rec.ts_ms == 0) {
        rec.ts_ms = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    }
    std::string line;
    lsw::base::LogFormat::append_line(
        line, static_cast<std::int64_t>(rec.ts_ms),
        lsw::base::LogFormat::level_from_name(rec.level.toStdString(), 2), rec.source.toStdString(),
        rec.msg.toStdString());
    lsw::base::LogTail::instance().append(line);

    pending_.push_back(std::move(rec));
    if (!scheduled_) {
        scheduled_ = true;
        QMetaObject::invokeMethod(this, "arm_flush", Qt::QueuedConnection);
    }
}

void LogStore::arm_flush() {
    qint64 delay = 0;
    {
        const std::lock_guard lock{mutex_};
        delay = kBatchMs - (QDateTime::currentMSecsSinceEpoch() - last_flush_ms_);
    }
    if (delay <= 0) {
        flush_pending();
    } else {
        QTimer::singleShot(static_cast<int>(delay), this, &LogStore::flush_pending);
    }
}

void LogStore::flush_pending() {
    QVector<LogRecord> batch;
    {
        const std::lock_guard lock{mutex_};
        batch.swap(pending_);
        scheduled_ = false;
        last_flush_ms_ = QDateTime::currentMSecsSinceEpoch();
    }
    if (batch.isEmpty()) {
        return;
    }
    for (const auto& record : batch) {
        if (!sources_.contains(record.source)) {
            sources_.push_back(record.source);
        }
    }
    total_ += static_cast<quint64>(batch.size());
    records_ += batch;
    if (records_.size() > kCapacity) {
        records_.remove(0, records_.size() - kCapacity);
    }
    Q_EMIT appended(batch);
}

}
