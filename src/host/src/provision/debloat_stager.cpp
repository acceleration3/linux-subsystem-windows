#include "lsw/host/provision/debloat_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/debloat/wim_debloater.hpp"
#include "lsw/host/provision/install/windows_iso_builder.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStorageInfo>
#include <QtConcurrent>

namespace lsw::host::provision {

namespace {

// Room for the extracted WIM plus the rebuilt copy wimlib writes
// beside it. The compressed entry on the ISO is most of the ISO, and
// the rebuild is a second copy of what survives.
constexpr qint64 kFreeSpaceSlackBytes = 2LL * 1024 * 1024 * 1024;

// Pull ``entry`` out of the ISO with 7z into ``dir``. Returns the
// extracted path, or empty on failure.
//
// 7z rather than libisofs, which is already this project's answer to
// this question — see WimEditionLister. Windows media is UDF and
// libisofs reads ISO9660 and Joliet only.
[[nodiscard]] QString extract_entry(const QString& iso_path, const QString& entry,
                                    const QString& dir) {
    QProcess seven_zip;
    seven_zip.start(QStringLiteral("7z"),
                    {QStringLiteral("e"), QStringLiteral("-y"), QStringLiteral("-bso0"),
                     QStringLiteral("-bsp0"), iso_path, entry, QStringLiteral("-o%1").arg(dir)});
    if (!seven_zip.waitForStarted(5000)) {
        LSW_WARN("debloat: 7z failed to start (is it installed?)");
        return {};
    }
    if (!seven_zip.waitForFinished(900000) || seven_zip.exitStatus() != QProcess::NormalExit
        || seven_zip.exitCode() != 0) {
        LSW_WARN("debloat: 7z extract of {} failed (exit={})", entry.toStdString(),
                 seven_zip.exitCode());
        return {};
    }
    const QString out = QDir(dir).filePath(QFileInfo(entry).fileName());
    return QFile::exists(out) ? out : QString{};
}

}  // namespace

DebloatStager::DebloatStager(QObject* parent) : QObject(parent) {}

DebloatStager::~DebloatStager() = default;

void DebloatStager::trim(const QString& iso_path, const QString& output_dir,
                         int image_index, debloat::DebloatLevel level) {
    scratch_ = std::make_unique<QTemporaryDir>();
    if (!scratch_->isValid()) {
        Q_EMIT failed(QStringLiteral("cannot create a scratch directory for the image"));
        return;
    }
    const QString dir = scratch_->path();

    // Refuse early rather than dying part-way through a rebuild: the
    // failure at the end of a ten-minute extract is the expensive one.
    const qint64 iso_size = QFileInfo(iso_path).size();
    if (const QStorageInfo storage{dir};
        storage.isValid() && storage.bytesAvailable() < iso_size + kFreeSpaceSlackBytes) {
        Q_EMIT failed(QStringLiteral("not enough free space to trim the image: %1 MiB needed "
                                     "under %2")
                          .arg((iso_size + kFreeSpaceSlackBytes) / (1024 * 1024))
                          .arg(dir));
        return;
    }

    const QPointer<DebloatStager> self{this};
    (void)QtConcurrent::run([self, iso_path, output_dir, dir, image_index, level]() {
        auto report = [self](const QString& detail, int percent) {
            QMetaObject::invokeMethod(self.data(), [self, detail, percent] {
                if (self) {
                    Q_EMIT self->progress(detail, percent);
                }
            });
        };
        auto fail = [self](const QString& message) {
            QMetaObject::invokeMethod(self.data(), [self, message] {
                if (self) {
                    Q_EMIT self->failed(message);
                }
            });
        };

        report(QStringLiteral("Extracting the Windows image..."), 0);
        // Win11 media ships install.wim; some SKUs ship install.esd,
        // which wimlib reads and writes just the same.
        QString wim = extract_entry(iso_path, QStringLiteral("sources/install.wim"), dir);
        if (wim.isEmpty()) {
            wim = extract_entry(iso_path, QStringLiteral("sources/install.esd"), dir);
        }
        if (wim.isEmpty()) {
            fail(QStringLiteral("could not extract sources/install.wim or install.esd from %1")
                     .arg(iso_path));
            return;
        }

        // Lives on this thread for the length of the call. Its progress
        // signal is emitted here and marshalled back by ``report``,
        // which is why the connection takes no context object.
        debloat::WimDebloater debloater;
        QObject::connect(&debloater, &debloat::WimDebloater::progress,
                         [&report](const QString& phase, int percent) {
                             report(phase, percent);
                         });

        QString error;
        if (!debloater.apply(wim, image_index, debloat::DebloatPlan{level}, &error)) {
            fail(error);
            return;
        }

        const qsizetype removed = debloater.removed().size();
        LSW_INFO("debloat: {} paths removed from the image", removed);

        // The trimmed image has to become the install.wim on bootable
        // media: Setup reads <InstallFrom> in the windowsPE pass, before
        // anything can go looking for a second CD, so a path on the
        // payload ISO is not one it can be given.
        report(QStringLiteral("Rebuilding the Windows ISO..."), 70);
        const QString out_iso =
            QDir(output_dir).filePath(QStringLiteral("windows-trimmed.iso"));
        QString iso_error;
        if (!install::WindowsIsoBuilder::build(iso_path, wim, out_iso, dir, &iso_error)) {
            fail(iso_error);
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self, out_iso] {
            if (!self) {
                return;
            }
            LSW_INFO("debloat: trimmed media ready at {}", out_iso.toStdString());
            Q_EMIT self->progress(QStringLiteral("Trimmed image ready"), 100);
            Q_EMIT self->finished(out_iso);
        });
    });
}

}  // namespace lsw::host::provision
