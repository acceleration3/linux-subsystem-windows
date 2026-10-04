#pragma once

#include "lsw/host/provision/install/payload_inputs.hpp"

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {

// Builds the secondary install ISO. Uses libisofs — small runtime dep,
// available everywhere libvirt is.
class PayloadIsoBuilder {
public:
    PayloadIsoBuilder() = delete;

    // Stage ``in`` and write it to ``in.output_iso_path``. False on
    // failure, with ``error_out`` saying which step failed and why.
    [[nodiscard]] static bool build(const PayloadInputs& in, QString* error_out);

    // Visible for testing — what we put on the ISO.
    [[nodiscard]] static QStringList stage_files(const PayloadInputs& in,
                                                 const QString& staging_dir);
};

}  // namespace lsw::host::provision::install
