#include "lsw/host/platform/guest_virtiofs_path.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString GuestVirtiofsPath::locate() {
    return locate_build_artifact(QStringLiteral("LSW_GUEST_VIRTIOFS"),
                                 QStringLiteral("/../../guest-virtiofs"),
                                 QStringLiteral("/../share/lsw/guest/virtiofs"));
}

}
