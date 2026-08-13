#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace lsw::host::apps {

// One log record as the manager sees it. Mirrors the agent's wire
// shape: {seq, ts_ms, level, msg}. ``level`` is the agent's string
// label ("INFO", "WARN", ...).
struct LogRecord {
    quint64 seq = 0;
    quint64 ts_ms = 0;
    QString level;
    QString msg;
};

}  // namespace lsw::host::apps

// Registered so records can cross threads through queued connections:
// they are produced on the channel's reader thread and consumed on the
// GUI thread.
Q_DECLARE_METATYPE(lsw::host::apps::LogRecord)
Q_DECLARE_METATYPE(QVector<lsw::host::apps::LogRecord>)
