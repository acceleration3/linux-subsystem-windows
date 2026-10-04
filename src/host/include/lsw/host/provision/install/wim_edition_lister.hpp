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

    // Which of ``editions`` to offer first: Pro, the most useful for an
    // LSW app-host VM, when the image has it, else the first. A position
    // in the list, 0 for an empty one. The trim tool and the wizard both
    // start from it, so the image trimmed is the edition the wizard
    // would have installed.
    [[nodiscard]] static int preferred(const QVector<WimEdition>& editions);
};

}  // namespace lsw::host::provision::install
