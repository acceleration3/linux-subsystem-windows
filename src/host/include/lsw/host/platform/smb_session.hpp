#pragma once

#include "lsw/host/platform/smb_attr.hpp"
#include "lsw/host/platform/smb_dir_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/statvfs.h>
#include <vector>

namespace lsw::host::platform {
class SmbSession {
public:
    using Handle = std::uint64_t;

    SmbSession() = default;
    virtual ~SmbSession() = default;
    SmbSession(const SmbSession&) = delete;
    SmbSession& operator=(const SmbSession&) = delete;

    virtual int stat(const std::string& path, SmbAttr& out) = 0;
    virtual int list(const std::string& path, std::vector<SmbDirEntry>& out) = 0;
    virtual int open(const std::string& path, int flags, Handle& handle) = 0;
    virtual std::int64_t read(Handle handle, std::uint64_t offset, char* buffer,
                              std::size_t size) = 0;
    virtual std::int64_t write(Handle handle, std::uint64_t offset, const char* buffer,
                               std::size_t size) = 0;
    virtual int truncate(Handle handle, std::uint64_t size) = 0;
    virtual int close(Handle handle) = 0;
    virtual int unlink(const std::string& path) = 0;
    virtual int rmdir(const std::string& path) = 0;
    virtual int mkdir(const std::string& path) = 0;
    virtual int rename(const std::string& from, const std::string& to) = 0;
    virtual int utimens(const std::string& path, const timespec& atime, const timespec& mtime) = 0;
    virtual int statvfs(const std::string& path, struct statvfs& out) = 0;
};

}  // namespace lsw::host::platform
