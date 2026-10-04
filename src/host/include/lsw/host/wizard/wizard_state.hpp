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
    QString payload_iso_path;         // built by the pipeline
    int host_cpu_threads = 4;         // detected at construction
    int host_ram_mib = 8192;          // detected at construction
    qint64 host_free_disk_bytes = 0;  // read again at every page
    // Why the pipeline stopped, when it failed; empty while it runs and
    // once it has finished. The last page reads it.
    QString failure;
    // Local Windows account credentials baked into autounattend.xml.
    QString user_name = "lsw";
    QString user_password = "lsw";
    QString computer_name;  // Windows hostname; empty → VM name
    // How much of Windows to remove from install.wim, and whether it
    // has already been removed.
    //
    // Trimming is a tool now, not a step, so the usual answer is "the
    // user did not run it". That does NOT make the preparation pass
    // optional: the setup bypasses live in the offline registry, and
    // without them Windows Setup refuses this domain outright. So an
    // untouched image still goes through the stager at ``None``, which
    // writes the bypasses and takes nothing out.
    provision::debloat::DebloatLevel debloat = provision::debloat::DebloatLevel::None;
    // Set when ``chosen_iso_path`` came out of the trim tool, which has
    // already done that pass.
    bool iso_prepared = false;
    // 1-based WIM image index chosen on VmSizePage (which Windows
    // edition to install). 1 when unknown / Microsoft-download path.
    int image_index = 1;
    // That edition's EDITIONID ("Core", "Professional", ...), which picks
    // the product key. Empty when the editions were not read.
    QString edition_id;
    // Microsoft download-portal state. ``ms_edition_ids`` is the list of
    // ``productEditionId`` values returned by IsoSourceFetcher.
    QList<int> fido_edition_ids;
    QString fido_edition_label;
    QString fido_preferred_arch = "x64";
    QString fido_download_url;
};

}  // namespace lsw::host::wizard
