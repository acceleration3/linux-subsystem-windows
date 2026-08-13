#include "lsw/host/platform/agent_exe_path.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace lsw::host::platform {

QString AgentExePath::locate() {
    const QString from_env = qEnvironmentVariable("LSW_AGENT_EXE");
    if (!from_env.isEmpty() && QFile::exists(from_env)) {
        return from_env;
    }
    const QString candidate = QDir::cleanPath(
        QCoreApplication::applicationDirPath()
        + "/../../../build-agent/src/agent/lsw-guest-agent.exe");
    return QFile::exists(candidate) ? candidate : QString{};
}

}  // namespace lsw::host::platform
