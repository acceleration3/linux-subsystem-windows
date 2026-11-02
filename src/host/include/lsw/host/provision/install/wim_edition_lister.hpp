#pragma once

#include "lsw/host/provision/install/wim_edition.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::provision::install {
class WimEditionLister {
public:
    WimEditionLister() = delete;

    [[nodiscard]] static QVector<WimEdition> list(const QString& iso_path,
                                                  const QString& scratch_dir = {},
                                                  QString* error = nullptr);

    [[nodiscard]] static int preferred(const QVector<WimEdition>& editions);
};

}
