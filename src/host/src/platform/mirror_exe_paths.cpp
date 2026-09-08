#include "lsw/host/platform/mirror_exe_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

namespace {

QString from(const char* variable, const char* build_tree_path) {
    const QString named = qEnvironmentVariable(variable);
    if (!named.isEmpty() && QFile::exists(named)) {
        return named;
    }
    const QString candidate =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + build_tree_path);
    return QFile::exists(candidate) ? candidate : QString{};
}

}  // namespace

QString MirrorExePaths::host() {
    return from("LSW_REMOTEWIN", "/../../remotewin/source/host/remotewin");
}

QString MirrorExePaths::guest() {
    return from("LSW_REMOTEWIN_GUEST",
                "/../../../build-agent/remotewin/source/guest/remotewin-guest.exe");
}

}  // namespace lsw::host::platform
