#pragma once

#include <QByteArray>
#include <QUuid>
#include <QtGlobal>

namespace lsw::host::platform {
class QemuProcess {
public:
    QemuProcess() = delete;

    [[nodiscard]] static qint64 pid_of(const QUuid& uuid);

    [[nodiscard]] static bool runs_domain(const QByteArray& cmdline, const QByteArray& uuid);
};

}
