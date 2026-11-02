#pragma once

#include "lsw/host/platform/smb_attr.hpp"

#include <string>

namespace lsw::host::platform {
struct SmbDirEntry {
    std::string name;
    SmbAttr attr;
};

}  // namespace lsw::host::platform
