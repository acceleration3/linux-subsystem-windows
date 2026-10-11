#pragma once

#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QString>

namespace lsw::host::provision {

// Inputs collected by the wizard pages (or by the CLI). The ISO
// source is exclusive: ``iso_local_path`` takes precedence; if
// empty, ``iso_download_url`` is used. Producing ``iso_download_url``
// from a Microsoft edition+language pair is the caller's job — see
// ``MicrosoftResolver``.
struct PipelineInputs {
    QString iso_local_path;
    QString iso_download_url;
    QString iso_display_label;
    QString user_name;
    QString user_password;
    QString computer_name;  // Windows hostname; empty → VM name
    config::VmSizing sizing;
    // 1-based WIM image index (which Windows edition to install).
    // Resolved from the ISO via install::WimEditionLister::list().
    int image_index = 1;
    // The chosen image's EDITIONID; the product key is chosen by it.
    // Empty when it was never read (the Microsoft download path).
    QString edition_id;
    // The language Windows is installed in, one the medium carries
    // (install::MediaLanguage), e.g. "en-GB". Empty takes the medium's
    // default.
    QString language;
    // How much of Windows to take out of the image before installing.
    debloat::DebloatLevel debloat = debloat::DebloatLevel::Sane;
    // Set when ``iso_local_path`` already names a trimmed image, which
    // is what the wizard hands over: it does the work on its own page,
    // under a progress bar, before the pipeline exists. False for the
    // unattended CLI path, where the pipeline still trims for itself.
    bool image_already_trimmed = false;

};

}  // namespace lsw::host::provision
