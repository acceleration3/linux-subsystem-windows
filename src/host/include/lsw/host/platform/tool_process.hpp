#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace lsw::host::platform {
[[nodiscard]] bool run_tool(const QString& program, const QStringList& args, int timeout_ms,
                            QByteArray* stdout_out, QString* error_out);

}
