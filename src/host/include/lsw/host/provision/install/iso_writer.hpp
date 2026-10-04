#pragma once

#include <QString>
#include <QVector>
#include <optional>

namespace lsw::host::provision::install {

// The one place that turns a directory into an ISO9660 image.
//
// libisofs is a C library with four things to free on every path out
// of a write — the image, the write options, the burn source and the
// output file — and two copies of that sequence had started to differ
// in which of them each error path remembered. Everything that writes
// an ISO goes through here.
//
// The payload ISO calls it with no boot record; rebuilt Windows media
// calls it with the two El Torito entries Windows ships.

// One entry in the El Torito catalog. Always no-emulation: it is what
// Windows media and anything else this project boots uses, and an
// emulated floppy image has never been wanted.
struct ElToritoEntry {
    // Inside the tree, as the ISO will carry it: "boot/etfsboot.com".
    QString image_path;
    // The El Torito platform id. 0 is x86 BIOS, 0xEF is EFI.
    int platform_id = 0;
    // 512-byte sectors the firmware loads. Part of the boot record, not
    // something libisofs can work out from the file; 0 leaves its
    // default.
    short load_sectors = 0;
};

struct ElToritoBoot {
    // Where the catalog goes inside the tree: "boot/boot.cat".
    QString catalog_path;
    // In catalog order. The first entry creates the catalog and is the
    // default; the rest are added to it.
    QVector<ElToritoEntry> entries;
};

struct IsoWriteOptions {
    QString volume_id;
    // Level 3 allows files past 4 GiB as multi-extent entries. Cheap to
    // keep on whatever the payload.
    int iso_level = 3;
    // What Windows reads long names from.
    bool joliet = true;
    // Keeps POSIX names sane when Linux mounts the image. Means nothing
    // to Windows Setup.
    bool rock_ridge = true;
};

// Write the contents of ``tree_dir`` to ``output_iso``, with ``boot``'s
// catalog when there is one. False on failure, with ``error_out``
// naming the step, and no partial file left behind.
[[nodiscard]] bool write_iso_from_tree(const QString& tree_dir, const QString& output_iso,
                                       const IsoWriteOptions& options,
                                       const std::optional<ElToritoBoot>& boot, QString* error_out);

}  // namespace lsw::host::provision::install
