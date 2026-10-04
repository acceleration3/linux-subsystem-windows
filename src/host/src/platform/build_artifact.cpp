#include "lsw/host/platform/build_artifact.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

QString locate_build_artifact(const QString& env_var, const QString& build_relative,
                              const QString& installed_relative) {
    if (!env_var.isEmpty()) {
        const QString named = qEnvironmentVariable(env_var.toLatin1().constData());
        if (!named.isEmpty() && QFile::exists(named)) {
            return named;
        }
    }
    const QString exe_dir = QCoreApplication::applicationDirPath();
    for (const QString& relative : {build_relative, installed_relative}) {
        if (relative.isEmpty()) {
            continue;
        }
        const QString candidate = QDir::cleanPath(exe_dir + relative);
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace lsw::host::platform
