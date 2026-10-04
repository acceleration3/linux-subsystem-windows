#include "lsw/host/provision/install/payload_iso.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/iso_writer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace lsw::host::provision::install {

QStringList PayloadIsoBuilder::stage_files(const PayloadInputs& in, const QString& staging_dir) {
    QStringList created;
    QDir dir{staging_dir};
    dir.mkpath(".");

    // autounattend.xml at the root — Windows setup auto-detects this.
    // All bootstrap steps (firewall rules, HKLM\Run registration, agent
    // copy) are baked into the autounattend's specialize-pass
    // RunSynchronous block; we no longer ship a separate
    // post-install.cmd / SetupComplete.cmd.
    const QString unattend_path = dir.filePath("autounattend.xml");
    QFile uf{unattend_path};
    if (uf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        uf.write(in.autounattend_xml.toUtf8());
        created << unattend_path;
    }

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

    // Each architecture's drivers go in a directory of their own, so
    // the whole set copies to the guest with one xcopy — and so a
    // 32-bit driver can never shadow its 64-bit namesake, which
    // ``opengl32.dll`` would otherwise do.
    //
    // Names on the ISO are the names the guest wants, not the names the
    // cross-build produced: the mingw ``lib`` prefix means nothing to
    // Windows, and ``rvk_icd.dll`` is what the agent's manifest points
    // at.
    auto stage_drivers = [&](const QString& subdir, const QString& vulkan_icd,
                             const QString& opengl_icd, const QString& opencl_icd,
                             const QString& loader) {
        if (vulkan_icd.isEmpty() && opengl_icd.isEmpty() && opencl_icd.isEmpty()) {
            return;
        }
        const QString driver_dir = dir.filePath(subdir);
        QDir().mkpath(driver_dir);
        auto stage_one = [&](const QString& src, const QString& name) {
            if (src.isEmpty()) {
                return;
            }
            const QString dest = QDir(driver_dir).filePath(name);
            if (!QFile::copy(src, dest)) {
                LSW_WARN("failed to stage {} -> {}", src.toStdString(), dest.toStdString());
                return;
            }
            created << dest;
        };
        stage_one(vulkan_icd, "rvk_icd.dll");
        stage_one(opengl_icd, "opengl32.dll");
        stage_one(opencl_icd, "rvk_opencl.dll");
        // The loader ships beside the drivers rather than in a
        // directory of its own: the agent links both out of the same
        // per-architecture folder, and one folder is one xcopy.
        stage_one(loader, "vulkan-1.dll");
    };
    stage_drivers("rvk", in.vulkan_icd_path, in.opengl_icd_path, in.opencl_icd_path,
                  in.vulkan_loader_path);
    stage_drivers("rvk32", in.vulkan_icd32_path, in.opengl_icd32_path, in.opencl_icd32_path,
                  in.vulkan32_loader_path);

    stage_optional(in.lsw_msi_path, "lsw.msi");
    stage_optional(in.dxvk_archive_path, "dxvk.tar.gz");
    stage_optional(in.vkd3d_archive_path, "vkd3d.tar.gz");
    stage_optional(in.agent_exe_path, "lsw-agent.exe");
    stage_optional(in.mirror_exe_path, "remotewin-guest.exe");

    return created;
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
    (void)stage_files(in, staging.path());

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
