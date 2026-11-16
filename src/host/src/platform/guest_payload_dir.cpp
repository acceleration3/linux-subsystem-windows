#include "lsw/host/platform/guest_payload_dir.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString GuestPayloadDir::locate(QStringView name) {
    return locate_build_artifact(QStringLiteral("LSW_GUEST_") + name.toString().toUpper(),
                                 QStringLiteral("/../../guest-") + name,
                                 QStringLiteral("/../share/lsw/guest/") + name);
}

}
