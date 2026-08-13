#pragma once

#include <QString>

namespace lsw::host::provision::install {

// One Windows edition inside a media file's ``sources/install.wim``.
// ``index`` is 1-based, matching what the autounattend's
// <InstallFrom>/<MetaData> expects.
struct WimEdition {
    int index = 0;
    QString name;  // e.g. "Windows 11 Pro"
};

}  // namespace lsw::host::provision::install
