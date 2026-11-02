#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {
[[nodiscard]] QString find_seven_zip();

[[nodiscard]] QString seven_zip_missing_message();

[[nodiscard]] bool run_seven_zip(const QStringList& args, int timeout_ms,
                                 QString* error_out);

[[nodiscard]] QStringList seven_zip_entries(const QString& archive_path, QString* error_out);

}
