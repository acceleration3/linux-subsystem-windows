#pragma once

#include "lsw/host/platform/smb_session.hpp"

#include <string>
#include <sys/types.h>
#include <unordered_map>

struct _SMBCCTX;
struct _SMBCFILE;

namespace lsw::host::platform {
class LibsmbSession : public SmbSession {
public:
    LibsmbSession(_SMBCCTX* context, std::string base);
    ~LibsmbSession() override;

    int stat(const std::string& path, SmbAttr& out) override;
    int list(const std::string& path, std::vector<SmbDirEntry>& out) override;
    int open(const std::string& path, int flags, Handle& handle) override;
    std::int64_t read(Handle handle, std::uint64_t offset, char* buffer, std::size_t size) override;
    std::int64_t write(Handle handle, std::uint64_t offset, const char* buffer,
                       std::size_t size) override;
    int truncate(Handle handle, std::uint64_t size) override;
    int close(Handle handle) override;
    int unlink(const std::string& path) override;
    int rmdir(const std::string& path) override;
    int mkdir(const std::string& path) override;
    int rename(const std::string& from, const std::string& to) override;
    int utimens(const std::string& path, const timespec& atime, const timespec& mtime) override;
    int statvfs(const std::string& path, struct statvfs& out) override;

private:
    struct Open {
        _SMBCFILE* file = nullptr;
        off_t position = 0;
    };

    [[nodiscard]] std::string url(const std::string& path) const;
    [[nodiscard]] Open* find(Handle handle);
    [[nodiscard]] bool seek(Open& open, std::uint64_t offset);

    _SMBCCTX* context_;
    std::string base_;
    std::unordered_map<Handle, Open> open_;
    Handle next_ = 1;
};

}  // namespace lsw::host::platform
