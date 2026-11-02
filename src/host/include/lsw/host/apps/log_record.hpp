#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace lsw::host::apps {
struct LogRecord {
    quint64 seq = 0;
    quint64 ts_ms = 0;
    QString level;
    QString source;
    QString msg;
};

}

Q_DECLARE_METATYPE(lsw::host::apps::LogRecord)
Q_DECLARE_METATYPE(QVector<lsw::host::apps::LogRecord>)
