#pragma once

#include "lsw/host/provision/install/payload_inputs.hpp"

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {
class PayloadIsoBuilder {
public:
    PayloadIsoBuilder() = delete;

    [[nodiscard]] static bool build(const PayloadInputs& in, QString* error_out);

    [[nodiscard]] static bool stage_files(const PayloadInputs& in, const QString& staging_dir,
                                          QStringList* created_out, QString* error_out);
};

}
