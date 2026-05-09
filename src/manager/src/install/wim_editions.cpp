#include "lsw/manager/install/wim_editions.hpp"

#include "lsw/log.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <wimlib.h>

namespace lsw::manager::install {

namespace {

// Headroom over the on-ISO compressed size — the extracted WIM is
// larger than its ISO entry, and we don't want to fill the disk.
constexpr qint64 kFreeSpaceSlackBytes = 2LL * 1024 * 1024 * 1024;  // 2 GiB

// Pull ``sources/<name>`` out of the ISO with 7z into ``dir``. Returns
// the extracted file path, or empty on failure. 7z reads the UDF
// filesystem Windows media uses (libisofs/xorriso only do
// ISO9660/Joliet) and streams the entry sequentially.
[[nodiscard]] QString extract_entry(const QString& iso_path, const QString& entry,
                                     const QString& dir) {
    QProcess p;
    p.start(QStringLiteral("7z"),
            {QStringLiteral("e"), QStringLiteral("-y"), QStringLiteral("-bso0"),
             QStringLiteral("-bsp0"), iso_path, entry,
             QStringLiteral("-o%1").arg(dir)});
    if (!p.waitForStarted(5000)) {
        LSW_WARN("wim_editions: 7z failed to start (is it installed?)");
        return {};
    }
    // Up to ~8 GB sequential read off an ISO — generous ceiling so a
    // slow spinning disk doesn't get cut off mid-extract.
    if (!p.waitForFinished(900000) || p.exitStatus() != QProcess::NormalExit
        || p.exitCode() != 0) {
        LSW_WARN("wim_editions: 7z extract of {} failed (exit={})",
                 entry.toStdString(), p.exitCode());
        return {};
    }
    const QString out = QDir(dir).filePath(QFileInfo(entry).fileName());
    return QFile::exists(out) ? out : QString{};
}

}  // namespace

QVector<WimEdition> list_iso_editions(const QString& iso_path, const QString& scratch_dir,
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

    // Win11 media ships install.wim; some SKUs / older builds ship a
    // compressed install.esd. libwim reads both.
    QString wim = extract_entry(iso_path, QStringLiteral("sources/install.wim"), tmp.path());
    if (wim.isEmpty()) {
        wim = extract_entry(iso_path, QStringLiteral("sources/install.esd"), tmp.path());
    }
    if (wim.isEmpty()) {
        return fail(QStringLiteral("could not extract sources/install.wim or install.esd "
                                     "from %1 (is 7z installed and is this a Windows ISO "
                                     "with enough scratch space?)")
                        .arg(iso_path));
    }

    WIMStruct* w = nullptr;
    const int rc = ::wimlib_open_wim(wim.toUtf8().constData(), 0, &w);
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

}  // namespace lsw::manager::install
