#include "lsw/host/platform/guest_network_path.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString GuestNetworkPath::locate() {
    return locate_build_artifact(QStringLiteral("LSW_GUEST_NETWORK"),
                                 QStringLiteral("/../../guest-network"),
                                 QStringLiteral("/../share/lsw/guest/network"));
}

}
