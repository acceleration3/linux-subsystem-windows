#include "lsw/host/provision/install/payload_iso.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

// We don't link libburn — tell libisofs to expose its own copy of
// ``struct burn_source`` rather than forward-declaring the libburn one.
#define LIBISOFS_WITHOUT_LIBBURN 1
#include <libisofs/libisofs.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace lsw::host::provision::install {

namespace {

// libisofs requires a one-time process-wide init; subsequent calls
// after the first are documented as no-ops but the standard pattern
// is to guard explicitly so we don't depend on internal behaviour.
void ensure_libisofs_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (iso_init() < 0) {
            LSW_ERROR("libisofs: iso_init failed");
        }
    });
}

}  // namespace

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
                LSW_WARN("failed to stage ivshmem driver file {}",
                         entry.fileName().toStdString());
                continue;
            }
            created << dest;
        }
    }

    // The Vulkan pair goes in its own directory so the whole thing can
    // be copied to the guest with one xcopy, manifest included.
    if (!in.icd_dll_path.isEmpty() || !in.vulkan_loader_path.isEmpty()) {
        const QString vk_dir = dir.filePath("vulkan");
        QDir().mkpath(vk_dir);
        auto stage_into_vulkan = [&](const QString& src, const QString& name) {
            if (src.isEmpty()) {
                return;
            }
            const QString dest = QDir(vk_dir).filePath(name);
            if (!QFile::copy(src, dest)) {
                LSW_WARN("failed to stage {} -> {}", src.toStdString(), dest.toStdString());
                return;
            }
            created << dest;
        };
        stage_into_vulkan(in.icd_dll_path, "lsw_icd.dll");
        stage_into_vulkan(in.vulkan_loader_path, "vulkan-1.dll");
        if (!in.icd_manifest_json.isEmpty()) {
            const QString manifest = QDir(vk_dir).filePath("lsw_icd.json");
            QFile mf{manifest};
            if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                mf.write(in.icd_manifest_json.toUtf8());
                created << manifest;
            }
        }
    }

    stage_optional(in.lsw_msi_path, "lsw.msi");
    stage_optional(in.dxvk_archive_path, "dxvk.tar.gz");
    stage_optional(in.vkd3d_archive_path, "vkd3d.tar.gz");
    stage_optional(in.agent_exe_path, "lsw-agent.exe");

    return created;
}

int PayloadIsoBuilder::build(const PayloadInputs& in) {
    QTemporaryDir staging;
    if (!staging.isValid()) {
        LSW_ERROR("could not create staging dir for payload ISO");
        return -1;
    }
    (void)stage_files(in, staging.path());

    ensure_libisofs_init();

    // 1. Build the in-memory image from the staging directory tree.
    IsoImage* image = nullptr;
    if (iso_image_new("LSW_PAYLOAD", &image) < 0 || image == nullptr) {
        LSW_ERROR("libisofs: iso_image_new failed");
        return -1;
    }
    IsoDir* root = iso_image_get_root(image);
    const auto staging_path_utf8 = staging.path().toUtf8();
    if (iso_tree_add_dir_rec(image, root, staging_path_utf8.constData()) < 0) {
        LSW_ERROR("libisofs: iso_tree_add_dir_rec({}) failed", staging.path().toStdString());
        iso_image_unref(image);
        return -1;
    }

    // 2. Configure the write options. Joliet is what Windows reads;
    //    Rock Ridge keeps long/POSIX names sane on Linux mounts of the
    //    same image; ISO level 3 supports >4 GiB files (cheap to keep
    //    on even though our payloads are tiny).
    IsoWriteOpts* opts = nullptr;
    if (iso_write_opts_new(&opts, /*profile=*/2) < 0 || opts == nullptr) {
        LSW_ERROR("libisofs: iso_write_opts_new failed");
        iso_image_unref(image);
        return -1;
    }
    iso_write_opts_set_iso_level(opts, 3);
    iso_write_opts_set_rockridge(opts, 1);
    iso_write_opts_set_joliet(opts, 1);

    // 3. Hand back a burn_source we can read out as a flat byte stream.
    struct burn_source* src = nullptr;
    if (iso_image_create_burn_source(image, opts, &src) < 0 || src == nullptr) {
        LSW_ERROR("libisofs: iso_image_create_burn_source failed");
        iso_write_opts_free(opts);
        iso_image_unref(image);
        return -1;
    }
    iso_write_opts_free(opts);

    // 4. Drain the burn source into the destination file. libisofs
    //    documents 2048-byte sectors as the natural read granularity.
    const auto out_path_utf8 = in.output_iso_path.toUtf8();
    std::FILE* out = std::fopen(out_path_utf8.constData(), "wb");
    if (out == nullptr) {
        LSW_ERROR("could not open {} for writing: {}", in.output_iso_path.toStdString(),
                  std::strerror(errno));
        if (src->free_data != nullptr) {
            src->free_data(src);
        }
        std::free(src);
        iso_image_unref(image);
        return -1;
    }

    constexpr int kSector = 2048;
    unsigned char buf[kSector];
    qint64 total_bytes = 0;
    int rc = 0;
    while (true) {
        // burn_source has both read() and read_xt(); newer-version
        // sources set read=NULL and use read_xt instead.
        const int got = (src->read != nullptr) ? src->read(src, buf, kSector)
                                               : src->read_xt(src, buf, kSector);
        if (got < 0) {
            LSW_ERROR("libisofs: burn_source read failed (rc={})", got);
            rc = -1;
            break;
        }
        if (got == 0) {
            break;  // EOF
        }
        if (std::fwrite(buf, 1, static_cast<size_t>(got), out) != static_cast<size_t>(got)) {
            LSW_ERROR("write {} failed: {}", in.output_iso_path.toStdString(),
                      std::strerror(errno));
            rc = -1;
            break;
        }
        total_bytes += got;
    }

    std::fclose(out);
    if (src->free_data != nullptr) {
        src->free_data(src);
    }
    std::free(src);
    iso_image_unref(image);

    if (rc != 0) {
        QFile::remove(in.output_iso_path);
        return rc;
    }
    LSW_INFO("payload ISO built at {} ({} bytes via libisofs)",
             in.output_iso_path.toStdString(), total_bytes);
    return 0;
}

}  // namespace lsw::host::provision::install
