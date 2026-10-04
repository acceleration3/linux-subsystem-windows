#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {

// The install image, once it is off the media and on disk.
//
// Windows ships it in one of three forms and the difference reaches
// every caller, so it is named here rather than rediscovered at each
// one:
//
//   install.wim   the usual thing.
//   install.esd   the same file under a tighter codec. Some SKUs, and
//                 anything that came down the consumer download path.
//   install.swm   a WIM cut into numbered pieces, each under the 4 GiB
//                 a FAT32 stick or an ISO9660 volume can hold. Media
//                 built for a USB drive ships this way — and so does
//                 this project's own trimmed output, which is why a
//                 trimmed ISO fed back in has to be readable.
//
// A split WIM is one image across several files. Part 1 holds the
// metadata, which is why it alone answers "what editions are in here";
// the file data is spread over all of them, so anything that reads
// content has to hand wimlib the rest as well.
struct StagedInstallImage {
    // Part 1 for a split WIM, the whole thing otherwise. This is what
    // wimlib_open_wim() is given.
    QString path;

    // The parts after the first, in part order. Empty unless split.
    // wimlib_reference_resource_files() wants these before any read of
    // file data; without them the resources living in them are missing
    // and wimlib says so as a resource error, not as a missing file.
    QStringList extra_parts;

    [[nodiscard]] bool is_split() const noexcept { return !extra_parts.isEmpty(); }

    // Every file staged, for the caller that wants to delete them once
    // it has what it needs. A split image is most of the media.
    [[nodiscard]] QStringList all_files() const {
        QStringList files{path};
        files.append(extra_parts);
        return files;
    }
};

// Pull the install image out of ``iso_path`` into ``dir``.
//
// Reads the medium's own table of contents rather than trying names in
// turn: which of the three forms is present, and how many pieces a
// split one is in, are answers the ISO already has. False on failure
// with ``error_out`` set to a sentence naming what was actually found,
// since "this ISO has no install image" and "7-Zip could not read this
// ISO" are different problems with the same symptom.
[[nodiscard]] bool stage_install_image(const QString& iso_path, const QString& dir,
                                       StagedInstallImage* out, QString* error_out);

}  // namespace lsw::host::provision::install
