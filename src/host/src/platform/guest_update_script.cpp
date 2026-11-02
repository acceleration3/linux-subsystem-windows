#include "lsw/host/platform/guest_update_script.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString GuestUpdateScript::locate() {
    return locate_build_artifact(QStringLiteral("LSW_GUEST_UPDATE_SCRIPT"),
                                 QStringLiteral("/../../guest-update/lsw-guest-update.ps1"),
                                 QStringLiteral("/../share/lsw/guest/lsw-guest-update.ps1"));
}

}  // namespace lsw::host::platform
