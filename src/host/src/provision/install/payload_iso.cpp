#include "lsw/host/provision/install/payload_iso.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/iso_writer.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace lsw::host::provision::install {

namespace {

// provision.ps1's $AdapterNames, relative to C:\av.
constexpr char kAdapterNamesFile[] = "adapters.txt";

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
    // All bootstrap steps (firewall rules, HKLM\Run registration, agent
    // copy) are baked into the autounattend's specialize-pass
    // RunSynchronous block; we no longer ship a separate
    // post-install.cmd / SetupComplete.cmd.
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

    auto stage_optional = [&](const QString& src, const QString& dest_name) {
        if (src.isEmpty()) {
            return;
        }
        const QString dest = dir.filePath(dest_name);
        if (!QFile::copy(src, dest)) {
            LSW_WARN("failed to stage {} -> {}", src.toStdString(), dest.toStdString());
            return;
        }
        created << dest;
    };
    // The IVSHMEM driver goes in its own directory: the autounattend's
    // DriverPaths point at a folder, not at files.
    if (!in.ivshmem_driver_dir.isEmpty()) {
        const QString dest_dir = dir.filePath("ivshmem");
        QDir().mkpath(dest_dir);
        const QDir src_dir{in.ivshmem_driver_dir};
        const auto entries = src_dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        if (entries.isEmpty()) {
            LSW_WARN("ivshmem driver dir {} is empty; nothing staged",
                     in.ivshmem_driver_dir.toStdString());
        }
        for (const auto& entry : entries) {
            const QString dest = QDir(dest_dir).filePath(entry.fileName());
            if (!QFile::copy(entry.absoluteFilePath(), dest)) {
                LSW_WARN("failed to stage ivshmem driver file {}", entry.fileName().toStdString());
                continue;
            }
            created << dest;
        }
    }

    // The accel-virt driver package, whole, as ``\av``. Copied as a
    // tree rather than file by file so the layout ``cmake --install``
    // produced is the layout the guest gets: provision.ps1 finds its
    // package in ``C:\av\package``, and the sign stage checks every
    // file against package.sha256. A file that failed to copy is a
    // package pnputil refuses, so unlike the optional files above it
    // fails the build.
    //
    // All but adapters.txt: accel-virt's install writes the GPUs of the
    // machine that built the package into it, and the guest's are this
    // machine's, written below from in.av_adapter_names.
    if (!in.av_driver_tree.isEmpty()) {
        const QDir src_root{in.av_driver_tree};
        for (const char* required : {"provision.ps1", "package/avgpu.inf", "package/avgpu.sys"}) {
            if (!QFileInfo::exists(src_root.filePath(QLatin1String(required)))) {
                return set_error(error_out,
                                 QStringLiteral("%1 is not an accel-virt driver package: it has no "
                                                "%2. Build it with `cmake --build build --target "
                                                "drivers && cmake --install build` in "
                                                "external/accel-virt, or point $LSW_AV_DRIVERS at "
                                                "an installed package")
                                     .arg(in.av_driver_tree, QLatin1String(required)));
            }
        }
        const QString dest_root = dir.filePath(QStringLiteral("av"));
        QDirIterator it{in.av_driver_tree, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories};
        while (it.hasNext()) {
            const QString src = it.next();
            const QString relative = src_root.relativeFilePath(src);
            if (relative == QLatin1String(kAdapterNamesFile)) {
                continue;
            }
            const QString dest = QDir(dest_root).filePath(relative);
            if (!QDir().mkpath(QFileInfo(dest).absolutePath()) || !QFile::copy(src, dest)) {
                return set_error(error_out,
                                 QStringLiteral("could not stage %1 from the accel-virt driver "
                                                "package into the payload")
                                     .arg(relative));
            }
            created << dest;
        }
        if (!in.av_adapter_names.isEmpty()) {
            const QString dest = QDir(dest_root).filePath(QLatin1String(kAdapterNamesFile));
            QFile names{dest};
            const QByteArray bytes = in.av_adapter_names.toUtf8();
            if (!names.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                names.write(bytes) != bytes.size() || !names.flush()) {
                return set_error(error_out, QStringLiteral("could not write %1: %2")
                                                .arg(dest, names.errorString()));
            }
            created << dest;
        }
    }

    // The Vulkan loaders, beside the package's OpenCL ones so the guest
    // installs every loader out of one folder per architecture. Not in
    // the package because accel-virt does not build them; they come from
    // this project's own cross builds.
    auto stage_loader = [&](const QString& src, const QString& arch) {
        if (src.isEmpty()) {
            return true;
        }
        const QString dest_dir = dir.filePath(QStringLiteral("av/loaders/%1").arg(arch));
        const QString dest = QDir(dest_dir).filePath(QStringLiteral("vulkan-1.dll"));
        // Ours replaces any the package might carry one day: it is the
        // one this host built and tested against.
        QFile::remove(dest);
        if (!QDir().mkpath(dest_dir) || !QFile::copy(src, dest)) {
            return set_error(error_out, QStringLiteral("could not stage the %1 Vulkan loader %2 "
                                                       "into the payload")
                                            .arg(arch, src));
        }
        created << dest;
        return true;
    };
    if (!stage_loader(in.vulkan_loader_x64_path, QStringLiteral("x64")) ||
        !stage_loader(in.vulkan_loader_x86_path, QStringLiteral("x86"))) {
        return false;
    }

    stage_optional(in.lsw_msi_path, "lsw.msi");
    stage_optional(in.agent_exe_path, "lsw-agent.exe");
    stage_optional(in.mirror_exe_path, "remotewin-guest.exe");

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
