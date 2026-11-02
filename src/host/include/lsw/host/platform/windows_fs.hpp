#pragma once

#include "lsw/host/platform/file_table.hpp"
#include "lsw/host/platform/session_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

namespace lsw::host::platform {
class WindowsFs {
public:
    using DirFill = std::function<void(const std::string& name, const struct stat* st)>;

    WindowsFs(SessionPool& pool, uid_t uid, gid_t gid);

    int getattr(const std::string& path, struct stat& st);
    int readdir(const std::string& path, const DirFill& fill);
    int open(const std::string& path, int flags, std::uint64_t& fh);
    int create(const std::string& path, int flags, std::uint64_t& fh);
    std::int64_t read(std::uint64_t fh, std::uint64_t offset, char* buffer, std::size_t size);
    std::int64_t write(std::uint64_t fh, std::uint64_t offset, const char* buffer,
                       std::size_t size);
    int release(std::uint64_t fh);
    int truncate(const std::string& path, std::uint64_t size);
    int ftruncate(std::uint64_t fh, std::uint64_t size);
    int unlink(const std::string& path);
    int rename(const std::string& from, const std::string& to, bool no_replace);
    int mkdir(const std::string& path);
    int rmdir(const std::string& path);
    int utimens(const std::string& path, const timespec times[2]);
    int statfs(const std::string& path, struct statvfs& out);

    [[nodiscard]] std::size_t open_files() const { return files_.size(); }

private:
    template<class Fn>
    int on_path(Fn&& fn, bool retry = false);
    template<class Fn>
    std::int64_t on_handle(std::uint64_t fh, Fn&& fn);
    std::int64_t striped(std::uint64_t fh, std::uint64_t offset, char* buffer, std::size_t size,
                         bool writing);
    std::int64_t transfer_part(FileTable::File& file, std::size_t lane, std::uint64_t offset,
                               char* buffer, std::size_t size, bool writing);
    static std::int64_t transfer(SmbSession& session, SmbSession::Handle handle,
                                 std::uint64_t offset, char* buffer, std::size_t size,
                                 bool writing);
    int open_with(const std::string& path, int flags, std::uint64_t& fh);

    SessionPool& pool_;
    FileTable files_;
    uid_t uid_;
    gid_t gid_;
};

}  // namespace lsw::host::platform
