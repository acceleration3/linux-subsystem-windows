#pragma once

#include "lsw/host/platform/smb_attr.hpp"

#include <sys/stat.h>
#include <sys/types.h>

namespace lsw::host::platform {
class SmbAttrConvert {
public:
    SmbAttrConvert() = delete;

    static void to_stat(const SmbAttr& attr, uid_t uid, gid_t gid, struct stat& out);
};

}  // namespace lsw::host::platform
