#pragma once

#include <cstdlib>

namespace lsw::host::config {

inline const char* const kVmName =
    std::getenv("LSW_DOMAIN_NAME") != nullptr && *std::getenv("LSW_DOMAIN_NAME") != '\0'
        ? std::getenv("LSW_DOMAIN_NAME")
        : "lsw";

}
