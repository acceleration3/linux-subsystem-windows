#include "lsw/host/mirror/icon_badge_file.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/windows_badge.hpp"
#include "lsw/host/config/vm_name.hpp"

#include <QProcessEnvironment>
#include <QSaveFile>

namespace lsw::host::mirror {

const QString& IconBadgeFile::path() {
    if (tried_) {
        return path_;
    }
    tried_ = true;
    const QString runtime = QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        LSW_WARN("mirror: no XDG_RUNTIME_DIR to keep the icon badge in; window icons get no Windows badge");
        return path_;
    }
    const QString path = QStringLiteral("%1/%2-icon-badge.bgra").arg(runtime, QLatin1String(config::kVmName));
    const QByteArray mark = apps::WindowsBadge::raw_mark();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(mark) != mark.size() || !file.commit()) {
        LSW_WARN("mirror: the icon badge {} could not be written ({}); window icons get no Windows badge",
                 path.toStdString(), file.errorString().toStdString());
        return path_;
    }
    path_ = path;
    return path_;
}

}
