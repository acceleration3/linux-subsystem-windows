#pragma once

#include <QString>

namespace lsw::host::provision::install {
struct WimEdition {
    int index = 0;
    QString name;
    QString edition_id;
};

}
