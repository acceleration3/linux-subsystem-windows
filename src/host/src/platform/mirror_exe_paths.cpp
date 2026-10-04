#include "lsw/host/platform/mirror_exe_paths.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

// The host half is a private executable, run by the manager and by
// nothing on a user's PATH, so it is installed under lib/lsw.
QString MirrorExePaths::host() {
    return locate_build_artifact(QStringLiteral("LSW_REMOTEWIN"),
                                 QStringLiteral("/../../remotewin/source/host/remotewin"),
                                 QStringLiteral("/../lib/lsw/remotewin"));
}

QString MirrorExePaths::guest() {
    return locate_build_artifact(
        QStringLiteral("LSW_REMOTEWIN_GUEST"),
        QStringLiteral("/../../../build-agent/remotewin/source/guest/remotewin-guest.exe"),
        QStringLiteral("/../share/lsw/guest/remotewin-guest.exe"));
}

}  // namespace lsw::host::platform
