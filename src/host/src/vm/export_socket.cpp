#include "lsw/host/vm/export_socket.hpp"

#include "lsw/host/config/vm_name.hpp"

#include <QProcessEnvironment>

namespace lsw::host::vm {

QString ExportSocket::path() {
    const QString runtime = QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        return {};
    }
    return QStringLiteral("%1/%2-export.sock").arg(runtime, QLatin1String(config::kVmName));
}

}  // namespace lsw::host::vm
