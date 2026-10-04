#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {

// The one place that knows how to find and run the 7-Zip command line.
//
// Reading a Windows ISO needs it: the media is UDF, and neither
// libisofs nor libarchive reads UDF — bsdtar on a Windows 11 ISO lists
// the two-file ISO9660 shim and nothing else. Every path that opens
// installation media goes through here.
//
// Which name it answers to depends on the distribution: Arch's
// ``7zip`` and the upstream tarball install ``7zz``, Debian's
// ``p7zip-full`` installs ``7z`` and ``7za``. Hardcoding one of them
// turns a missing package into "could not extract sources/install.wim",
// which reads as a broken ISO.

// Path to the 7-Zip binary, or empty when none is installed.
// ``$LSW_SEVEN_ZIP`` overrides the search — a name on PATH or an
// absolute path both work. Not cached: somebody who installs the
// package with the dialog open should be able to press the button
// again rather than restart the manager.
[[nodiscard]] QString find_seven_zip();

// Why nothing happened, in a sentence a person can act on. Names the
// packages rather than the binary, since installing is the fix.
[[nodiscard]] QString seven_zip_missing_message();

// Run 7-Zip with ``args``, waiting at most ``timeout_ms``. ``error_out``
// gets a sentence on every failure path, the missing binary included,
// with whatever 7-Zip put on stderr appended when it said anything.
[[nodiscard]] bool run_seven_zip(const QStringList& args, int timeout_ms,
                                 QString* error_out);

// Every file in ``archive_path``, by the path 7-Zip names it with —
// forward slashes, and the case the medium itself carries. Directories
// are left out: no caller has ever wanted one.
//
// Asking beats guessing. Windows media names its install image four
// different ways depending on how it was made (install.wim, install.esd,
// or install.swm plus a numbered part per further piece), and a reader
// that guesses gets to report "not a Windows ISO" about media that is.
[[nodiscard]] QStringList seven_zip_entries(const QString& archive_path, QString* error_out);

}  // namespace lsw::host::provision::install
