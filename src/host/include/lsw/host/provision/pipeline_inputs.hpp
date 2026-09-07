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
    QString virtio_iso_path;
    QString user_name;
    QString user_password;
    QString computer_name;  // Windows hostname; empty → VM name
    config::VmSizing sizing;
    // 1-based WIM image index (which Windows edition to install).
    // Resolved from the ISO via install::WimEditionLister::list().
    int image_index = 1;
    // How much of Windows to take out of the image before installing.
    debloat::DebloatLevel debloat = debloat::DebloatLevel::Sane;

};

}  // namespace lsw::host::provision
