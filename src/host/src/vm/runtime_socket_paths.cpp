#include "lsw/host/vm/runtime_socket_paths.hpp"

#include "lsw/host/config/vm_name.hpp"

#include <QProcessEnvironment>

namespace lsw::host::vm {

QString RuntimeSocketPaths::named(const char* suffix) {
    const QString runtime = QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        return {};
    }
    return QStringLiteral("%1/%2-%3.sock").arg(runtime, QLatin1String(config::kVmName), QLatin1String(suffix));
}

QString RuntimeSocketPaths::export_path() { return named("export"); }

QString RuntimeSocketPaths::input_path() { return named("input"); }

}
