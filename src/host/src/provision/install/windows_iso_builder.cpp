#include "lsw/host/provision/install/windows_iso_builder.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/seven_zip.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#define LIBISOFS_WITHOUT_LIBBURN 1
#include <libisofs/libisofs.h>

#include <wimlib.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace lsw::host::provision::install {

namespace {
constexpr auto kBiosBoot = "boot/etfsboot.com";
constexpr auto kUefiBoot = "efi/microsoft/boot/efisys.bin";
constexpr auto kBootCatalog = "boot/boot.cat";

constexpr int kPlatformEfi = 0xEF;

constexpr short kBiosLoadSectors = 8;

constexpr qint64 kSwmPartBytes = 3800LL * 1024 * 1024;  // NOLINT: qint64 literal

[[nodiscard]] bool unpack_iso(const QString& iso_path, const QString& dir, QString* error_out) {
    return run_seven_zip({QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-bso0"),
                          QStringLiteral("-bsp0"), iso_path, QStringLiteral("-o%1").arg(dir)},
                         1800000, error_out);
}

[[nodiscard]] bool place_image(const QString& trimmed_wim, const QString& sources_dir,
                               QString* error_out) {
    QDir sources{sources_dir};
    for (const QString& name :
         sources.entryList({QStringLiteral("install.wim"), QStringLiteral("install.esd"),
                            QStringLiteral("install*.swm")},
                           QDir::Files)) {
        QFile::remove(sources.filePath(name));
    }

    const qint64 size = QFileInfo(trimmed_wim).size();
    if (size < WindowsIsoBuilder::kIso9660FileLimit) {
        const QString dest = QDir(sources_dir).filePath(QStringLiteral("install.wim"));
        if (!QFile::copy(trimmed_wim, dest)) {
            if (error_out != nullptr) {
                *error_out = QStringLiteral("could not place the trimmed image at %1").arg(dest);
            }
            return false;
        }
        return true;
    }

    LSW_INFO("iso: trimmed image is {} MiB, past the ISO9660 file limit; splitting",
             size / (1024 * 1024));
    WIMStruct* wim = nullptr;
    if (const int rc = wimlib_open_wim(trimmed_wim.toUtf8().constData(), 0, &wim); rc != 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not reopen the trimmed image to split it");
        }
        return false;
    }
    const QString swm = QDir(sources_dir).filePath(QStringLiteral("install.swm"));
    const int rc = wimlib_split(wim, swm.toUtf8().constData(), kSwmPartBytes, 0);
    wimlib_free(wim);
    if (rc != 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not split the trimmed image into .swm parts");
        }
        return false;
    }
    return true;
}

}

bool WindowsIsoBuilder::build(const QString& source_iso, const QString& trimmed_wim,
                              const QString& output_iso, const QString& scratch_dir,
                              QString* error_out) {
    const QString tree = QDir(scratch_dir).filePath(QStringLiteral("iso-tree"));
    QDir().mkpath(tree);
    LSW_INFO("iso: unpacking {} into {}", source_iso.toStdString(), tree.toStdString());
    if (!unpack_iso(source_iso, tree, error_out)) {
        return false;
    }

    const QString sources = QDir(tree).filePath(QStringLiteral("sources"));
    if (!QDir(sources).exists()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("%1 has no sources directory, so it is not Windows media")
                             .arg(source_iso);
        }
        return false;
    }
    if (!place_image(trimmed_wim, sources, error_out)) {
        return false;
    }

    const QString boot_bios = QDir(tree).filePath(QLatin1String(kBiosBoot));
    const QString boot_uefi = QDir(tree).filePath(QLatin1String(kUefiBoot));
    if (!QFile::exists(boot_bios) || !QFile::exists(boot_uefi)) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("%1 is missing its boot images; it cannot be rebuilt "
                                        "into bootable media")
                             .arg(source_iso);
        }
        return false;
    }

    if (iso_init() < 0) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs would not initialise");
        }
        return false;
    }

    IsoImage* image = nullptr;
    if (iso_image_new("LSW_WINDOWS", &image) < 0 || image == nullptr) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not create the image");
        }
        return false;
    }

    IsoDir* root = iso_image_get_root(image);
    if (root == nullptr
        || iso_tree_add_dir_rec(image, root, tree.toUtf8().constData()) < 0) {
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not add the unpacked tree");
        }
        return false;
    }

    ElToritoBootImage* bios = nullptr;
    if (iso_image_set_boot_image(image, kBiosBoot, ELTORITO_NO_EMUL, kBootCatalog, &bios) < 0
        || bios == nullptr) {
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not set the BIOS boot image");
        }
        return false;
    }
    el_torito_set_load_size(bios, kBiosLoadSectors);

    ElToritoBootImage* uefi = nullptr;
    if (iso_image_add_boot_image(image, kUefiBoot, ELTORITO_NO_EMUL, 0, &uefi) < 0
        || uefi == nullptr) {
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not add the UEFI boot image");
        }
        return false;
    }
    el_torito_set_boot_platform_id(uefi, kPlatformEfi);

    IsoWriteOpts* opts = nullptr;
    if (iso_write_opts_new(&opts, 2) < 0 || opts == nullptr) {
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not create write options");
        }
        return false;
    }
    iso_write_opts_set_iso_level(opts, 3);
    iso_write_opts_set_joliet(opts, 1);
    iso_write_opts_set_rockridge(opts, 0);

    struct burn_source* src = nullptr;
    if (iso_image_create_burn_source(image, opts, &src) < 0 || src == nullptr) {
        iso_write_opts_free(opts);
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: could not create the output stream");
        }
        return false;
    }
    iso_write_opts_free(opts);

    const auto out_path = output_iso.toUtf8();
    std::FILE* out = std::fopen(out_path.constData(), "wb");
    if (out == nullptr) {
        if (src->free_data != nullptr) {
            src->free_data(src);
        }
        std::free(src);
        iso_image_unref(image);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not open %1 for writing: %2")
                             .arg(output_iso, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    constexpr int kSector = 2048;
    unsigned char buf[kSector];
    qint64 total = 0;
    bool ok = true;
    while (ok) {
        const int got = (src->read != nullptr) ? src->read(src, buf, kSector)
                                               : src->read_xt(src, buf, kSector);
        if (got < 0) {
            if (error_out != nullptr) {
                *error_out = QStringLiteral("libisofs: read failed while writing the ISO");
            }
            ok = false;
            break;
        }
        if (got == 0) {
            break;
        }
        if (std::fwrite(buf, 1, static_cast<size_t>(got), out) != static_cast<size_t>(got)) {
            if (error_out != nullptr) {
                *error_out = QStringLiteral("writing %1 failed: %2")
                                 .arg(output_iso, QString::fromLocal8Bit(std::strerror(errno)));
            }
            ok = false;
            break;
        }
        total += got;
    }

    std::fclose(out);
    if (src->free_data != nullptr) {
        src->free_data(src);
    }
    std::free(src);
    iso_image_unref(image);

    if (!ok) {
        QFile::remove(output_iso);
        return false;
    }
    LSW_INFO("iso: wrote {} ({} MiB)", output_iso.toStdString(), total / (1024 * 1024));
    return true;
}

}
