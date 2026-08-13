#pragma once

#include "lsw/protocol/transport.hpp"
#include <cstdint>
#include <string>

namespace lsw::icd {

// Payload of the HELLO frame the ICD sends on connect.
struct ClientHello {
    std::string app_name;
    std::uint32_t pid = 0;
};

}  // namespace lsw::icd
