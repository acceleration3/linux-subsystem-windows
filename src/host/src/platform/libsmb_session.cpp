#include "lsw/host/platform/libsmb_session.hpp"

#include "lsw/host/platform/smb_path.hpp"

#include <cerrno>
#include <fcntl.h>
#include <libsmbclient.h>
#include <sys/stat.h>

namespace lsw::host::platform {

namespace {

constexpr std::uint16_t kDirectoryAttribute = 0x10;

int fail() {
    return errno != 0 ? -errno : -EIO;
}

SmbAttr from_stat(const struct stat& st) {
    SmbAttr attr;
    attr.directory = S_ISDIR(st.st_mode);
    attr.size = static_cast<std::uint64_t>(st.st_size);
    attr.atime = st.st_atim;
    attr.mtime = st.st_mtim;
    attr.ctime = st.st_ctim;
    return attr;
}

SmbAttr from_info(const libsmb_file_info& info) {
    SmbAttr attr;
    attr.directory = (info.attrs & kDirectoryAttribute) != 0;
    attr.size = info.size;
    attr.atime = info.atime_ts;
    attr.mtime = info.mtime_ts;
    attr.ctime = info.ctime_ts;
    return attr;
}

}  // namespace

LibsmbSession::LibsmbSession(SMBCCTX* context, std::string base)
    : context_(context), base_(std::move(base)) {}

LibsmbSession::~LibsmbSession() {
    smbc_free_context(context_, 1);
}

std::string LibsmbSession::url(const std::string& path) const {
    return SmbPath::url(base_, path);
}

LibsmbSession::Open* LibsmbSession::find(Handle handle) {
    const auto it = open_.find(handle);
    return it == open_.end() ? nullptr : &it->second;
}

bool LibsmbSession::seek(Open& open, std::uint64_t offset) {
    if (static_cast<std::uint64_t>(open.position) == offset) {
        return true;
    }
    const off_t at =
        smbc_getFunctionLseek(context_)(context_, open.file, static_cast<off_t>(offset), SEEK_SET);
    if (at < 0) {
        return false;
    }
    open.position = at;
    return true;
}

int LibsmbSession::stat(const std::string& path, SmbAttr& out) {
    struct stat st{};
    errno = 0;
    if (smbc_getFunctionStat(context_)(context_, url(path).c_str(), &st) < 0) {
        return fail();
    }
    out = from_stat(st);
    return 0;
}

int LibsmbSession::list(const std::string& path, std::vector<SmbDirEntry>& out) {
    errno = 0;
    SMBCFILE* dir = smbc_getFunctionOpendir(context_)(context_, url(path).c_str());
    if (dir == nullptr) {
        return fail();
    }
    const auto next = smbc_getFunctionReaddirPlus(context_);
    while (const libsmb_file_info* info = next(context_, dir)) {
        out.push_back({info->name, from_info(*info)});
    }
    smbc_getFunctionClosedir(context_)(context_, dir);
    return 0;
}

int LibsmbSession::open(const std::string& path, int flags, Handle& handle) {
    errno = 0;
    SMBCFILE* file = smbc_getFunctionOpen(context_)(context_, url(path).c_str(), flags, 0644);
    if (file == nullptr) {
        return fail();
    }
    handle = next_++;
    open_.emplace(handle, Open{file, 0});
    return 0;
}

std::int64_t LibsmbSession::read(Handle handle, std::uint64_t offset, char* buffer,
                                 std::size_t size) {
    Open* open = find(handle);
    if (open == nullptr) {
        return -EBADF;
    }
    errno = 0;
    if (!seek(*open, offset)) {
        return fail();
    }
    const ssize_t n = smbc_getFunctionRead(context_)(context_, open->file, buffer, size);
    if (n < 0) {
        open->position = -1;
        return fail();
    }
    open->position += n;
    return n;
}

std::int64_t LibsmbSession::write(Handle handle, std::uint64_t offset, const char* buffer,
                                  std::size_t size) {
    Open* open = find(handle);
    if (open == nullptr) {
        return -EBADF;
    }
    errno = 0;
    if (!seek(*open, offset)) {
        return fail();
    }
    const ssize_t n = smbc_getFunctionWrite(context_)(context_, open->file, buffer, size);
    if (n < 0) {
        open->position = -1;
        return fail();
    }
    open->position += n;
    return n;
}

int LibsmbSession::truncate(Handle handle, std::uint64_t size) {
    Open* open = find(handle);
    if (open == nullptr) {
        return -EBADF;
    }
    errno = 0;
    if (smbc_getFunctionFtruncate(context_)(context_, open->file, static_cast<off_t>(size)) < 0) {
        return fail();
    }
    return 0;
}

int LibsmbSession::close(Handle handle) {
    const auto it = open_.find(handle);
    if (it == open_.end()) {
        return -EBADF;
    }
    SMBCFILE* file = it->second.file;
    open_.erase(it);
    errno = 0;
    return smbc_getFunctionClose(context_)(context_, file) < 0 ? fail() : 0;
}

int LibsmbSession::unlink(const std::string& path) {
    errno = 0;
    return smbc_getFunctionUnlink(context_)(context_, url(path).c_str()) < 0 ? fail() : 0;
}

int LibsmbSession::rmdir(const std::string& path) {
    errno = 0;
    return smbc_getFunctionRmdir(context_)(context_, url(path).c_str()) < 0 ? fail() : 0;
}

int LibsmbSession::mkdir(const std::string& path) {
    errno = 0;
    return smbc_getFunctionMkdir(context_)(context_, url(path).c_str(), 0755) < 0 ? fail() : 0;
}

int LibsmbSession::rename(const std::string& from, const std::string& to) {
    errno = 0;
    return smbc_getFunctionRename(context_)(context_, url(from).c_str(), context_,
                                            url(to).c_str()) < 0
               ? fail()
               : 0;
}

int LibsmbSession::utimens(const std::string& path, const timespec& atime, const timespec& mtime) {
    timeval times[2];
    times[0].tv_sec = atime.tv_sec;
    times[0].tv_usec = atime.tv_nsec / 1000;
    times[1].tv_sec = mtime.tv_sec;
    times[1].tv_usec = mtime.tv_nsec / 1000;
    errno = 0;
    return smbc_getFunctionUtimes(context_)(context_, url(path).c_str(), times) < 0 ? fail() : 0;
}

int LibsmbSession::statvfs(const std::string& path, struct statvfs& out) {
    errno = 0;
    const std::string target = url(path);
    if (smbc_getFunctionStatVFS(context_)(context_, const_cast<char*>(target.c_str()), &out) < 0) {
        return fail();
    }
    const unsigned long unit = out.f_bsize * out.f_frsize;
    out.f_bsize = unit;
    out.f_frsize = unit;
    out.f_flag = 0;
    out.f_namemax = 255;
    return 0;
}

}  // namespace lsw::host::platform
