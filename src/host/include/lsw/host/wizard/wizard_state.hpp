#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QList>
#include <QString>

namespace lsw::host::wizard {
struct WizardState {
    config::VmRecord draft;
    QString chosen_iso_path;
    QString payload_iso_path;
    int host_cpu_threads = 4;
    int host_ram_mib = 8192;
    qint64 host_free_disk_bytes = 0;
    QString failure;
    QString user_name = "lsw";
    QString user_password = "lsw";
    QString computer_name;
    provision::debloat::DebloatLevel debloat = provision::debloat::DebloatLevel::None;
    bool iso_prepared = false;
    int image_index = 1;
    QString edition_id;
    QString language;
    QList<int> fido_edition_ids;
    QString fido_edition_label;
    QString fido_preferred_arch = "x64";
    QString fido_download_url;
};

}
