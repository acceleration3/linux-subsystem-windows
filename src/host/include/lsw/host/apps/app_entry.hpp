#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lsw::host::apps {

// One installed Windows app the guest agent told us about.
struct AppEntry {
    QString id;        // stable identifier (e.g. AUMID or full exe path)
    QString name;      // display name
    QString exe_path;  // absolute path inside the guest
    QImage icon;       // PNG decoded from the agent reply
    QStringList extensions;  // file types it handles, ".xlsx" and the like
};

}  // namespace lsw::host::apps
