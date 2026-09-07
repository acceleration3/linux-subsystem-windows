#pragma once

#include <QString>

namespace lsw::host::provision::install {

// Rebuilds a bootable Windows ISO around a trimmed install.wim.
//
// The trimmed image has to arrive as the install.wim on the boot media:
// Setup reads <InstallFrom> in the windowsPE pass, before anything can
// go looking for a second CD, so the path has to be one Setup already
// knows. That means a new ISO rather than a second one alongside.
//
// Two things about Windows media make this more than a repack.
//
// It boots two ways. The El Torito catalog carries a BIOS entry
// (boot/etfsboot.com) and a UEFI one (efi/microsoft/boot/efisys.bin,
// platform id 0xEF), both no-emulation. An ISO with only the first
// will not boot the OVMF firmware this project's domains use.
//
// And install.wim is usually larger than 4 GiB, which ISO9660 cannot
// hold — the reason Microsoft ships UDF, and libisofs writes no UDF at
// all. So an oversized image is split into install.swm parts under the
// limit, which Setup reads natively; it is the same thing DISM's
// /Split-Image does for FAT32 USB media.
class WindowsIsoBuilder {
public:
    WindowsIsoBuilder() = delete;

    // Unpack ``source_iso``, put ``trimmed_wim`` in place of its
    // install.wim, and write ``output_iso``.
    //
    // ``scratch_dir`` holds the unpacked tree and needs room for the
    // whole ISO again. False on failure, with ``error_out`` naming the
    // step.
    [[nodiscard]] static bool build(const QString& source_iso, const QString& trimmed_wim,
                                    const QString& output_iso, const QString& scratch_dir,
                                    QString* error_out);

    // Largest a file may be for ISO9660. Anything at or above this is
    // split; exposed so the caller can report why it is splitting.
    static constexpr qint64 kIso9660FileLimit = 4LL * 1024 * 1024 * 1024;
};

}  // namespace lsw::host::provision::install
