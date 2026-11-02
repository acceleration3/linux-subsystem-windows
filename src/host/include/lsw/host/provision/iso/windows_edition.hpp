#pragma once

#include "lsw/host/provision/iso/windows_edition.hpp"
#include <QList>
#include <QString>

namespace lsw::host::provision::iso {
struct WindowsEdition {
    QString display;
    QString version;
    QString release;
    QList<int> product_edition_ids;
};

}
