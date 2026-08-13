#pragma once

#include "lsw/host/provision/install/payload_inputs.hpp"

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {

// Builds the secondary install ISO. Uses libisofs — small runtime dep,
// available everywhere libvirt is. Returns 0/-1 (logged).
class PayloadIsoBuilder {
public:
    PayloadIsoBuilder() = delete;

    [[nodiscard]] static int build(const PayloadInputs& in);

    // Visible for testing — what we put on the ISO.
    [[nodiscard]] static QStringList stage_files(const PayloadInputs& in,
                                                 const QString& staging_dir);
};

}  // namespace lsw::host::provision::install
