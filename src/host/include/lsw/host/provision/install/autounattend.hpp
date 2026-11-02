#pragma once

#include "lsw/host/provision/install/autounattend_inputs.hpp"

#include <QString>

namespace lsw::host::provision::install {
class Autounattend {
public:
    Autounattend() = delete;

    [[nodiscard]] static QString render(const AutounattendInputs& in);
};

}
