#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QList>
#include <QString>

namespace lsw::host::wizard {

// Everything the pages accumulate before the pipeline starts. Passed by
// reference to each page; the pages write, the Wizard reads it once to
// build the Pipeline's inputs.
//
// A plain value type on purpose: it is the wizard's scratch pad, not a
// service. Once ``Pipeline::start`` is called nothing here is consulted
// again — the later pages are presentation only.
struct WizardState {
    config::VmRecord draft;           // accumulated by the pages
    QString chosen_iso_path;          // either downloaded or user-picked
    QString virtio_iso_path;          // downloaded virtio-win.iso
    QString payload_iso_path;         // built by the pipeline
    int host_cpu_threads = 4;         // detected at construction
    int host_ram_mib = 8192;          // detected at construction
    qint64 host_free_disk_bytes = 0;  // detected at construction
    // Local Windows account credentials baked into autounattend.xml.
    QString user_name = "lsw";
    QString user_password = "lsw";
    QString computer_name;  // Windows hostname; empty → VM name
    // How much of Windows to remove from install.wim before the guest
    // is installed from it. Chosen on DebloatPage.
    provision::debloat::DebloatLevel debloat = provision::debloat::DebloatLevel::Sane;
    // 1-based WIM image index chosen on VmSizePage (which Windows
    // edition to install). 1 when unknown / Microsoft-download path.
    int image_index = 1;
    // Microsoft download-portal state. ``ms_edition_ids`` is the list of
    // ``productEditionId`` values returned by IsoSourceFetcher.
    QList<int> fido_edition_ids;
    QString fido_edition_label;
    QString fido_preferred_arch = "x64";
    QString fido_download_url;
    bool microsoft_banned = false;
};

}  // namespace lsw::host::wizard
