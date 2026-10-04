#include "lsw/host/platform/agent_exe_path.hpp"

#include "lsw/host/platform/build_artifact.hpp"

namespace lsw::host::platform {

QString AgentExePath::locate() {
    // Installed under its build name; the payload stager gives it its
    // guest name (lsw-agent.exe) on the ISO.
    return locate_build_artifact(
        QStringLiteral("LSW_AGENT_EXE"),
        QStringLiteral("/../../../build-agent/src/agent/lsw-guest-agent.exe"),
        QStringLiteral("/../share/lsw/guest/lsw-guest-agent.exe"));
}

}  // namespace lsw::host::platform
