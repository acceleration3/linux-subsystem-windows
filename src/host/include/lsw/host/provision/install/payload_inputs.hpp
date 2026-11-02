#pragma once

#include "lsw/host/services/guest_bundle.hpp"

#include <QString>

#include <memory>

namespace lsw::host::provision::install {
struct PayloadInputs {
    QString output_iso_path;
    QString autounattend_xml;

    std::shared_ptr<const services::GuestBundle::Contents> bundle;
};

}
