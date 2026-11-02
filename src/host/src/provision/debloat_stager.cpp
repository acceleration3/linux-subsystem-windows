#include "lsw/host/provision/debloat_stager.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/debloat/wim_debloater.hpp"
#include "lsw/host/provision/install/install_image.hpp"
#include "lsw/host/provision/install/seven_zip.hpp"
#include "lsw/host/provision/install/windows_iso_builder.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <memory>

namespace lsw::host::provision {

namespace {
constexpr qint64 kFreeSpaceSlackBytes = 2LL * 1024 * 1024 * 1024;

void remove_stale_scratch(const QString& output_dir) {
    QDir parent{output_dir};
    for (const QString& name :
         parent.entryList({QStringLiteral("lsw-trim-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = parent.filePath(name);
        if (QDir{path}.removeRecursively()) {
            LSW_INFO("debloat: removed a leftover scratch directory at {}", path.toStdString());
        }
    }
}

}

DebloatStager::DebloatStager(QObject* parent) : QObject(parent) {}

DebloatStager::~DebloatStager() = default;

void DebloatStager::trim(const QString& iso_path, const QString& output_dir,
                         int image_index, debloat::DebloatLevel level, const QString& file_name) {
    if (install::find_seven_zip().isEmpty()) {
        Q_EMIT failed(install::seven_zip_missing_message());
        return;
    }

    QDir().mkpath(output_dir);
    remove_stale_scratch(output_dir);

    auto scratch = std::make_shared<QTemporaryDir>(
        QDir(output_dir).filePath(QStringLiteral("lsw-trim-XXXXXX")));
    if (!scratch->isValid()) {
        Q_EMIT failed(QStringLiteral("cannot create a scratch directory for the image"));
        return;
    }
    const QString dir = scratch->path();

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
    const QString out_name = file_name.isEmpty()
                                 ? QStringLiteral("windows-trimmed.iso")
                                 : file_name;
    (void)QtConcurrent::run([self, scratch, iso_path, output_dir, dir, image_index, level,
                             out_name]() {
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
        install::StagedInstallImage image;
        QString stage_error;
        if (!install::stage_install_image(iso_path, dir, &image, &stage_error)) {
            fail(stage_error);
            return;
        }

        report(QStringLiteral("Isolating the chosen edition..."), 5);
        const QString single = QDir(dir).filePath(QStringLiteral("install-single.wim"));
        QString isolate_error;
        if (!debloat::WimDebloater::isolate_edition(image.path, image.extra_parts, image_index,
                                                    single, &isolate_error)) {
            fail(isolate_error);
            return;
        }
        for (const QString& part : image.all_files()) {
            QFile::remove(part);
        }

        debloat::WimDebloater debloater;
        QObject::connect(&debloater, &debloat::WimDebloater::progress,
                         [&report](const QString& phase, int percent) {
                             report(phase, percent);
                         });

        QString error;
        if (!debloater.apply(single, 1, debloat::DebloatPlan{level}, &error)) {
            fail(error);
            return;
        }

        const qsizetype removed = debloater.removed().size();
        LSW_INFO("debloat: {} paths removed from the image", removed);

        report(QStringLiteral("Rebuilding the Windows ISO..."), 70);
        const QString out_iso = QDir(output_dir).filePath(out_name);
        QString iso_error;
        if (!install::WindowsIsoBuilder::build(iso_path, single, out_iso, dir, &iso_error)) {
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

}
