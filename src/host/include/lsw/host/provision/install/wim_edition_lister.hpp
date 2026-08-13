#pragma once

#include "lsw/host/provision/install/wim_edition.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::provision::install {

// Reads the edition table out of a Windows ISO's install.wim.
//
// Its own class rather than a loose function so the "which editions does
// this medium offer" question has one named owner — the wizard's edition
// combo and the ``--edition`` CLI flag are both asking it.
class WimEditionLister {
public:
    WimEditionLister() = delete;

    // List the editions in ``iso_path``. ``scratch_dir`` is where the
    // WIM is extracted to if the ISO must be unpacked first; empty means
    // a temporary directory. On failure returns an empty list and, if
    // ``error`` is non-null, writes the reason to it.
    [[nodiscard]] static QVector<WimEdition> list(const QString& iso_path,
                                                  const QString& scratch_dir = {},
                                                  QString* error = nullptr);
};

}  // namespace lsw::host::provision::install
