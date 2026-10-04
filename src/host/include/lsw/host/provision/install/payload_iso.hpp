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

    // Visible for testing — lay out in ``staging_dir`` what goes on
    // the ISO, appending each file written to ``created_out`` when it
    // is given. Optional files that fail to copy are logged and left
    // out; the answer file and the driver package are not optional, so
    // a failure there is false with ``error_out`` saying which file.
    [[nodiscard]] static bool stage_files(const PayloadInputs& in, const QString& staging_dir,
                                          QStringList* created_out, QString* error_out);
};

}  // namespace lsw::host::provision::install
