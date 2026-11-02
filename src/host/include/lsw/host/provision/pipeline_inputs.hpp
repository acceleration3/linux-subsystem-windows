#pragma once

#include "lsw/host/config/vm_sizing.hpp"
#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QString>

namespace lsw::host::provision {
struct PipelineInputs {
    QString iso_local_path;
    QString iso_download_url;
    QString iso_display_label;
    QString user_name;
    QString user_password;
    QString computer_name;
    config::VmSizing sizing;
    int image_index = 1;
    QString edition_id;
    QString language;
    debloat::DebloatLevel debloat = debloat::DebloatLevel::Sane;
    bool image_already_trimmed = false;

};

}
