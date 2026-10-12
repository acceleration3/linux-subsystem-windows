#pragma once

#include <QString>

namespace lsw::host::provision::install {

// The Windows ISO as the domain boots it: without "Press any key to
// boot from CD or DVD".
//
// The prompt is the UEFI boot image's, efi/microsoft/boot/efisys.bin, a
// floppy image the El Torito catalog points the firmware at. Windows
// media ships a second one beside it, efisys_noprompt.bin, of the same
// size and the same loader less the prompt. Nobody is at the console
// during an unattended install to press the key, and a key sent in from
// the host lands or misses on timing.
//
// So the domain boots a qcow2 overlay on the ISO in which the sectors
// the catalog names hold the no-prompt image: a couple of megabytes,
// made in a second, and the ISO itself is never written, so it stays
// the same file for the next VM and the trim tool. Without the prompt
// the ISO would boot on every restart, which the domain answers by
// putting its disk first: empty, it falls through to the ISO; once
// Setup has put Windows on it, the ISO is never reached.
class NoPromptMedia {
public:
    NoPromptMedia() = delete;

    // Write the overlay on ``source_iso`` to ``overlay_path``, replacing
    // any file there. Blocks for 7-Zip and qemu-img, a second or two;
    // false on failure, with ``error_out`` saying why.
    [[nodiscard]] static bool make(const QString& source_iso, const QString& overlay_path,
                                   QString* error_out);
};

}  // namespace lsw::host::provision::install
