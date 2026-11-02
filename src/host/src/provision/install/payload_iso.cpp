#include "lsw/host/provision/install/payload_iso.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/iso_writer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace lsw::host::provision::install {

namespace {

bool set_error(QString* error_out, const QString& message) {
    LSW_ERROR("payload: {}", message.toStdString());
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

}  // namespace

bool PayloadIsoBuilder::stage_files(const PayloadInputs& in, const QString& staging_dir,
                                    QStringList* created_out, QString* error_out) {
    QStringList discard;
    QStringList& created = created_out != nullptr ? *created_out : discard;
    QDir dir{staging_dir};
    if (!dir.mkpath(".")) {
        return set_error(
            error_out,
            QStringLiteral("could not create the staging directory %1").arg(staging_dir));
    }

    // autounattend.xml at the root — Windows setup auto-detects this.
    // It makes the SetupComplete that runs the release under ``\update``.
    const QString unattend_path = dir.filePath("autounattend.xml");
    // A missing or short answer file is the failure with nothing to say
    // why: the ISO attaches and Windows shows its interactive installer.
    QFile uf{unattend_path};
    const QByteArray unattend = in.autounattend_xml.toUtf8();
    if (!uf.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        uf.write(unattend) != unattend.size() || !uf.flush()) {
        return set_error(error_out, QStringLiteral("could not write the answer file to %1: %2")
                                        .arg(unattend_path, uf.errorString()));
    }
    uf.close();
    created << unattend_path;

    if (!in.bundle || !in.bundle->error.isEmpty()) {
        return set_error(error_out, in.bundle ? QStringLiteral("LSW's files for Windows could not be "
                                                               "put together: %1").arg(in.bundle->error)
                                              : QStringLiteral("no files for Windows to carry"));
    }
    // The release the way an update stages it, so that the script reads it
    // from here exactly as it reads one it was sent.
    const QDir update{dir.filePath(QStringLiteral("update"))};
    const auto write = [&](const QString& relative, const QByteArray& bytes) {
        const QString dest = update.filePath(relative);
        QFile file{dest};
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(bytes) != bytes.size() || !file.flush()) {
            return set_error(error_out, QStringLiteral("could not write %1 into the payload: %2")
                                            .arg(relative, file.errorString()));
        }
        created << dest;
        return true;
    };
    for (const auto& file : in.bundle->files) {
        // The staged path is Windows': its backslashes are directories in the
        // guest, and on this side would be part of one long file name.
        const QString relative =
            QString::fromStdString(services::GuestBundle::staged_path(file.guest_path)).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!write(relative, file.bytes)) {
            return false;
        }
    }
    if (!write(QStringLiteral("manifest.txt"), QByteArray::fromStdString(in.bundle->manifest)) ||
        !write(QStringLiteral("shares.txt"), QByteArray::fromStdString(in.bundle->shares))) {
        return false;
    }

    return true;
}

bool PayloadIsoBuilder::build(const PayloadInputs& in, QString* error_out) {
    QTemporaryDir staging;
    if (!staging.isValid()) {
        LSW_ERROR("could not create staging dir for payload ISO");
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not create a temporary directory to stage the "
                                        "payload in: %1")
                             .arg(staging.errorString());
        }
        return false;
    }
    if (!stage_files(in, staging.path(), nullptr, error_out)) {
        return false;
    }

    IsoWriteOptions options;
    options.volume_id = QStringLiteral("LSW_PAYLOAD");
    if (!write_iso_from_tree(staging.path(), in.output_iso_path, options, std::nullopt,
                             error_out)) {
        return false;
    }
    LSW_INFO("payload ISO built at {} ({} bytes via libisofs)", in.output_iso_path.toStdString(),
             QFileInfo(in.output_iso_path).size());
    return true;
}

}  // namespace lsw::host::provision::install
