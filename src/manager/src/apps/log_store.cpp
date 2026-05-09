#include "lsw/manager/apps/log_store.hpp"

#include "lsw/manager/vm/agent_rpc_client.hpp"

#include <QMetaObject>

#include <nlohmann/json.hpp>

namespace lsw::manager::apps {

LogStore::LogStore(QObject* parent) : QObject(parent) {
    qRegisterMetaType<LogRecord>("lsw::manager::apps::LogRecord");
    qRegisterMetaType<QVector<LogRecord>>("QVector<lsw::manager::apps::LogRecord>");
}

void LogStore::attach(vm::AgentRpcClient& rpc) {
    // Handler runs on the rpc reader thread — marshal onto our own
    // (GUI) thread so the records_/signals stay single-threaded.
    // LogStore lives for the App's lifetime, so we don't keep the
    // token — the handler is fine to outlive any individual call.
    (void)rpc.on_event(QStringLiteral("log"), [this](const nlohmann::json& frame) {
        LogRecord rec;
        rec.seq = frame.value("seq", std::uint64_t{0});
        rec.ts_ms = frame.value("ts_ms", std::uint64_t{0});
        rec.level = QString::fromStdString(frame.value("level", std::string{}));
        rec.msg = QString::fromStdString(frame.value("msg", std::string{}));
        QMetaObject::invokeMethod(this, "on_record", Qt::QueuedConnection,
                                   Q_ARG(lsw::manager::apps::LogRecord, rec));
    });
}

void LogStore::clear() {
    records_.clear();
    max_seq_seen_ = 0;
    Q_EMIT cleared();
}

void LogStore::on_record(LogRecord rec) {
    // Dedupe: on reconnect the agent restarts pushing from seq 0 of
    // its current ring, which may overlap with what we already have.
    if (rec.seq <= max_seq_seen_) {
        return;
    }
    max_seq_seen_ = rec.seq;
    records_.push_back(rec);
    if (records_.size() > kCapacity) {
        const auto excess = static_cast<qsizetype>(records_.size() - kCapacity);
        records_.remove(0, static_cast<int>(excess));
    }
    QVector<LogRecord> batch{rec};
    Q_EMIT appended(batch);
}

}  // namespace lsw::manager::apps
