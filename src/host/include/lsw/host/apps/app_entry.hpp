#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lsw::host::apps {
struct AppEntry {
    QString id;
    QString name;
    QString exe_path;
    QString args;
    QImage icon;
    QStringList extensions;
};

}
