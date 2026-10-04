#include "lsw/host/platform/mirror_exe_paths.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString MirrorExePaths::host() {
    return locate_build_artifact(QStringLiteral("LSW_REMOTEWIN"),
                                 QStringLiteral("/../../remotewin/source/host/remotewin"));
}

QString MirrorExePaths::guest() {
    return locate_build_artifact(
        QStringLiteral("LSW_REMOTEWIN_GUEST"),
        QStringLiteral("/../../../build-agent/remotewin/source/guest/remotewin-guest.exe"));
}

}  // namespace lsw::host::platform
