#pragma once

#include <QImage>
#include <QString>

namespace lsw::host::apps {

// One installed Windows app the guest agent told us about.
struct AppEntry {
    QString id;        // stable identifier (e.g. AUMID or full exe path)
    QString name;      // display name
    QString exe_path;  // absolute path inside the guest
    QImage icon;       // PNG decoded from the agent reply
};

}  // namespace lsw::host::apps
