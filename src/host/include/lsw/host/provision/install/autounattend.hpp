#pragma once

#include "lsw/host/provision/install/autounattend_inputs.hpp"

#include <QString>

namespace lsw::host::provision::install {

// Renders ``autounattend.xml``: windowsPE + specialize + oobeSystem
// passes, with a FirstLogonCommand that chains into the post-install
// script on the payload ISO.
class Autounattend {
public:
    Autounattend() = delete;

    [[nodiscard]] static QString render(const AutounattendInputs& in);
};

}  // namespace lsw::host::provision::install
