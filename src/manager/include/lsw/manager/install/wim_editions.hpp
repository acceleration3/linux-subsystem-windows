#pragma once

#include <QString>
#include <QVector>

namespace lsw::manager::install {

// One Windows edition (WIM image) found inside an install media ISO.
// ``index`` is the 1-based WIM image index passed to Setup via
// autounattend's ``<ImageInstall>/<OSImage>/<InstallFrom>`` metadata.
struct WimEdition {
    int index = 0;
    QString name;  // e.g. "Windows 11 Pro"
};

// Enumerate the Windows editions in ``iso_path``'s
// ``sources/install.wim`` (or ``install.esd``).
//
// Windows 11 media uses a UDF filesystem that libisofs/xorriso can't
// read, and this host may have no loop device, so we can't mount it.
// We extract just the WIM/ESD with ``7z`` (sequential read off the
// ISO) into a temp dir under ``scratch_dir``, read its image table via
// libwim, then delete the extracted file.
//
// ``scratch_dir`` MUST be on a disk-backed filesystem with room for
// the whole image (modern install.wim is 5–8 GB; the default system
// /tmp is often a small tmpfs and will ENOSPC). Pass something like
// ``Settings::cache_dir()``. If empty, the ISO's own directory is
// used (it's guaranteed disk-backed and usually has room).
//
// Returns an empty vector and sets ``*error`` (if non-null) on any
// failure (7z missing, no install.wim, not enough scratch space,
// libwim open failure).
[[nodiscard]] QVector<WimEdition> list_iso_editions(const QString& iso_path,
                                                    const QString& scratch_dir = {},
                                                    QString* error = nullptr);

}  // namespace lsw::manager::install
