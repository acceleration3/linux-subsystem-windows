#include "lsw/host/provision/install/wim_edition_lister.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/install_image.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <wimlib.h>

namespace lsw::host::provision::install {

namespace {

// Headroom over the on-ISO compressed size — the extracted WIM is
// larger than its ISO entry, and we don't want to fill the disk.
constexpr qint64 kFreeSpaceSlackBytes = 2LL * 1024 * 1024 * 1024;  // 2 GiB

}  // namespace

QVector<WimEdition> WimEditionLister::list(const QString& iso_path, const QString& scratch_dir,
                                      QString* error) {
    const auto fail = [&](const QString& msg) -> QVector<WimEdition> {
        if (error != nullptr) {
            *error = msg;
        }
        LSW_WARN("wim_editions: {}", msg.toStdString());
        return {};
    };

    // Scratch must be disk-backed with room for the whole image. Default
    // to the ISO's own directory (guaranteed on disk, usually has room
    // since the multi-GB ISO already lives there).
    QString base = scratch_dir;
    if (base.isEmpty()) {
        base = QFileInfo(iso_path).absolutePath();
    }
    if (!QDir().mkpath(base)) {
        return fail(QStringLiteral("scratch dir %1 is not writable").arg(base));
    }

    // Bail early if there clearly isn't room — extracting most of an
    // 8 GB WIM only to ENOSPC wastes minutes and leaves a huge stub.
    const qint64 iso_size = QFileInfo(iso_path).size();
    const QStorageInfo si{base};
    if (si.isValid() && si.bytesAvailable() < iso_size + kFreeSpaceSlackBytes) {
        return fail(QStringLiteral("not enough free space in %1 (%2 MiB free, need "
                                     "~%3 MiB) to stage the install image")
                        .arg(base)
                        .arg(si.bytesAvailable() / (1024 * 1024))
                        .arg((iso_size + kFreeSpaceSlackBytes) / (1024 * 1024)));
    }

    QTemporaryDir tmp{QDir(base).filePath(QStringLiteral("lsw-wim-XXXXXX"))};
    tmp.setAutoRemove(true);
    if (!tmp.isValid()) {
        return fail(QStringLiteral("could not create a temp dir under %1").arg(base));
    }

    StagedInstallImage image;
    QString stage_error;
    if (!stage_install_image(iso_path, tmp.path(), &image, &stage_error)) {
        return fail(stage_error);
    }

    // No reference to the other parts of a split image, deliberately.
    // The edition table is metadata and all of it is in part 1; nothing
    // below reads a single byte of file data, so the parts that carry
    // it are not needed to answer this question.
    WIMStruct* w = nullptr;
    const int rc = ::wimlib_open_wim(image.path.toUtf8().constData(), 0, &w);
    if (rc != 0 || w == nullptr) {
        return fail(QStringLiteral("wimlib_open_wim failed: %1")
                        .arg(QString::fromUtf8(::wimlib_get_error_string(
                            static_cast<wimlib_error_code>(rc)))));
    }

    struct wimlib_wim_info info {};
    QVector<WimEdition> out;
    if (::wimlib_get_wim_info(w, &info) == 0) {
        for (int i = 1; i <= static_cast<int>(info.image_count); ++i) {
            const char* name = ::wimlib_get_image_name(w, i);
            WimEdition e;
            e.index = i;
            e.name = (name != nullptr && *name != '\0')
                         ? QString::fromUtf8(name)
                         : QStringLiteral("Image %1").arg(i);
            const char* edition_id = ::wimlib_get_image_property(w, i, "WINDOWS/EDITIONID");
            e.edition_id = edition_id != nullptr ? QString::fromUtf8(edition_id) : QString{};
            out.push_back(std::move(e));
        }
    }
    ::wimlib_free(w);

    if (out.isEmpty()) {
        return fail(QStringLiteral("WIM opened but reported zero images"));
    }
    LSW_INFO("wim_editions: {} editions in {}", out.size(), iso_path.toStdString());
    return out;
}

int WimEditionLister::preferred(const QVector<WimEdition>& editions) {
    for (int i = 0; i < editions.size(); ++i) {
        if (editions[i].edition_id.compare(QStringLiteral("Professional"), Qt::CaseInsensitive) ==
            0) {
            return i;
        }
    }
    // No EDITIONID to go by: the name, as the edition table spells it.
    for (int i = 0; i < editions.size(); ++i) {
        if (editions[i].edition_id.isEmpty() &&
            editions[i].name.contains(QStringLiteral("Pro"), Qt::CaseInsensitive)) {
            return i;
        }
    }
    return 0;
}

}  // namespace lsw::host::provision::install
