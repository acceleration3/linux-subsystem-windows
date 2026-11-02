#include "lsw/host/platform/smb_attr_convert.hpp"

#include <cstring>

namespace lsw::host::platform {

namespace {
constexpr blksize_t kBlockSize = 65536;
}

void SmbAttrConvert::to_stat(const SmbAttr& attr, uid_t uid, gid_t gid, struct stat& out) {
    std::memset(&out, 0, sizeof out);
    out.st_uid = uid;
    out.st_gid = gid;
    if (attr.directory) {
        out.st_mode = S_IFDIR | 0755;
        out.st_nlink = 2;
        out.st_size = 0;
    } else {
        out.st_mode = S_IFREG | 0644;
        out.st_nlink = 1;
        out.st_size = static_cast<off_t>(attr.size);
    }
    out.st_blksize = kBlockSize;
    out.st_blocks = static_cast<blkcnt_t>((attr.size + 511) / 512);
    out.st_atim = attr.atime;
    out.st_mtim = attr.mtime;
    out.st_ctim = attr.ctime;
}

}  // namespace lsw::host::platform
