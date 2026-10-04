#include "lsw/host/platform/build_artifact.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

QString locate_build_artifact(const QString& env_var, const QString& relative_to_exe) {
    const QString named = qEnvironmentVariable(env_var.toLatin1().constData());
    if (!named.isEmpty() && QFile::exists(named)) {
        return named;
    }
    const QString candidate =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + relative_to_exe);
    return QFile::exists(candidate) ? candidate : QString{};
}

}  // namespace lsw::host::platform
