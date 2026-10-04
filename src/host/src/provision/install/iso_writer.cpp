#include "lsw/host/provision/install/iso_writer.hpp"

#include "lsw/base/log.hpp"

#include <QFile>

// We don't link libburn — tell libisofs to expose its own copy of
// ``struct burn_source`` rather than forward-declaring the libburn one.
#define LIBISOFS_WITHOUT_LIBBURN 1
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <libisofs/libisofs.h>
#include <memory>
#include <mutex>

namespace lsw::host::provision::install {

namespace {

struct ImageUnref {
    void operator()(IsoImage* image) const noexcept { iso_image_unref(image); }
};

struct WriteOptsFree {
    void operator()(IsoWriteOpts* opts) const noexcept { iso_write_opts_free(opts); }
};

// A burn source is the caller's to dispose of in two steps: its own
// data first, then the struct itself.
struct BurnSourceFree {
    void operator()(burn_source* src) const noexcept {
        if (src->free_data != nullptr) {
            src->free_data(src);
        }
        std::free(src);
    }
};

struct FileClose {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

using ImagePtr = std::unique_ptr<IsoImage, ImageUnref>;
using WriteOptsPtr = std::unique_ptr<IsoWriteOpts, WriteOptsFree>;
using BurnSourcePtr = std::unique_ptr<burn_source, BurnSourceFree>;
using FilePtr = std::unique_ptr<std::FILE, FileClose>;

// libisofs requires a one-time process-wide init. Repeat calls are
// documented as harmless, but guarding means not depending on that.
[[nodiscard]] bool ensure_libisofs_init() {
    static std::once_flag once;
    static int rc = 0;
    std::call_once(once, [] { rc = iso_init(); });
    return rc >= 0;
}

// "<what> (<libisofs's reason>)", for the end of an error sentence.
void fail(QString* error_out, const QString& what, int rc) {
    const QString reason = QString::fromLocal8Bit(iso_error_to_msg(rc));
    LSW_ERROR("libisofs: {} ({})", what.toStdString(), reason.toStdString());
    if (error_out != nullptr) {
        *error_out = QStringLiteral("libisofs: %1 (%2)").arg(what, reason);
    }
}

void fail_errno(QString* error_out, const QString& what) {
    const QString reason = QString::fromLocal8Bit(std::strerror(errno));
    LSW_ERROR("{}: {}", what.toStdString(), reason.toStdString());
    if (error_out != nullptr) {
        *error_out = QStringLiteral("%1: %2").arg(what, reason);
    }
}

[[nodiscard]] bool add_boot(IsoImage* image, const ElToritoBoot& boot, QString* error_out) {
    for (qsizetype i = 0; i < boot.entries.size(); ++i) {
        const ElToritoEntry& entry = boot.entries.at(i);
        const QByteArray path = entry.image_path.toUtf8();
        ElToritoBootImage* added = nullptr;
        // The first call creates the catalog, and every further entry
        // is added to the set it made.
        const int rc =
            i == 0 ? iso_image_set_boot_image(image, path.constData(), ELTORITO_NO_EMUL,
                                              boot.catalog_path.toUtf8().constData(), &added)
                   : iso_image_add_boot_image(image, path.constData(), ELTORITO_NO_EMUL, 0, &added);
        if (rc < 0 || added == nullptr) {
            fail(error_out, QStringLiteral("could not add the boot image %1").arg(entry.image_path),
                 rc);
            return false;
        }
        if (entry.platform_id != 0) {
            el_torito_set_boot_platform_id(added, static_cast<std::uint8_t>(entry.platform_id));
        }
        if (entry.load_sectors > 0) {
            el_torito_set_load_size(added, entry.load_sectors);
        }
    }
    return true;
}

// Drain ``src`` into ``out``. libisofs documents 2048-byte sectors as
// the natural read granularity.
[[nodiscard]] bool drain(burn_source* src, std::FILE* out, const QString& output_iso,
                         QString* error_out) {
    constexpr int kSector = 2048;
    unsigned char buf[kSector];
    for (;;) {
        // burn_source has both read() and read_xt(); newer-version
        // sources set read=NULL and use read_xt instead.
        const int got =
            (src->read != nullptr) ? src->read(src, buf, kSector) : src->read_xt(src, buf, kSector);
        if (got < 0) {
            fail(error_out, QStringLiteral("read failed while writing %1").arg(output_iso), got);
            return false;
        }
        if (got == 0) {
            return true;
        }
        if (std::fwrite(buf, 1, static_cast<size_t>(got), out) != static_cast<size_t>(got)) {
            fail_errno(error_out, QStringLiteral("writing %1 failed").arg(output_iso));
            return false;
        }
    }
}

[[nodiscard]] bool write(const QString& tree_dir, const QString& output_iso,
                         const IsoWriteOptions& options, const std::optional<ElToritoBoot>& boot,
                         QString* error_out) {
    if (!ensure_libisofs_init()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs would not initialise");
        }
        return false;
    }

    IsoImage* raw_image = nullptr;
    if (const int rc = iso_image_new(options.volume_id.toUtf8().constData(), &raw_image);
        rc < 0 || raw_image == nullptr) {
        fail(error_out, QStringLiteral("could not create the image"), rc);
        return false;
    }
    const ImagePtr image{raw_image};

    IsoDir* root = iso_image_get_root(image.get());
    if (root == nullptr) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("libisofs: the new image has no root directory");
        }
        return false;
    }
    if (const int rc = iso_tree_add_dir_rec(image.get(), root, tree_dir.toUtf8().constData());
        rc < 0) {
        fail(error_out, QStringLiteral("could not add %1 to the image").arg(tree_dir), rc);
        return false;
    }

    if (boot.has_value() && !add_boot(image.get(), *boot, error_out)) {
        return false;
    }

    IsoWriteOpts* raw_opts = nullptr;
    if (const int rc = iso_write_opts_new(&raw_opts, /*profile=*/2);
        rc < 0 || raw_opts == nullptr) {
        fail(error_out, QStringLiteral("could not create write options"), rc);
        return false;
    }
    const WriteOptsPtr opts{raw_opts};
    iso_write_opts_set_iso_level(opts.get(), options.iso_level);
    iso_write_opts_set_joliet(opts.get(), options.joliet ? 1 : 0);
    iso_write_opts_set_rockridge(opts.get(), options.rock_ridge ? 1 : 0);

    burn_source* raw_src = nullptr;
    if (const int rc = iso_image_create_burn_source(image.get(), opts.get(), &raw_src);
        rc < 0 || raw_src == nullptr) {
        fail(error_out, QStringLiteral("could not create the output stream"), rc);
        return false;
    }
    const BurnSourcePtr src{raw_src};

    FilePtr out{std::fopen(output_iso.toUtf8().constData(), "wb")};
    if (out == nullptr) {
        fail_errno(error_out, QStringLiteral("could not open %1 for writing").arg(output_iso));
        return false;
    }
    if (!drain(src.get(), out.get(), output_iso, error_out)) {
        return false;
    }
    // Closed by hand because the close can fail: it is where buffered
    // bytes meet a full disk.
    if (std::fclose(out.release()) != 0) {
        fail_errno(error_out, QStringLiteral("writing %1 failed").arg(output_iso));
        return false;
    }
    return true;
}

}  // namespace

bool write_iso_from_tree(const QString& tree_dir, const QString& output_iso,
                         const IsoWriteOptions& options, const std::optional<ElToritoBoot>& boot,
                         QString* error_out) {
    if (!write(tree_dir, output_iso, options, boot, error_out)) {
        // A truncated ISO is worse than none: it attaches, and fails
        // somewhere inside Setup.
        QFile::remove(output_iso);
        return false;
    }
    return true;
}

}  // namespace lsw::host::provision::install
