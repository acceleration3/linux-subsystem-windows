#include "lsw/host/platform/windows_fs.hpp"

#include "lsw/host/platform/smb_attr_convert.hpp"
#include "lsw/host/platform/smb_errno.hpp"
#include "lsw/host/platform/smb_path.hpp"

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <future>
#include <vector>

namespace lsw::host::platform {

namespace {

constexpr std::size_t kStripes = 2;
constexpr std::size_t kStripeMin = 256 * 1024;
constexpr std::size_t kStripeAlign = 4096;
constexpr int kKeptOpenFlags = O_ACCMODE | O_TRUNC | O_CREAT | O_EXCL;

timespec resolve(const timespec& wanted, const timespec& current) {
    if (wanted.tv_nsec == UTIME_OMIT) {
        return current;
    }
    if (wanted.tv_nsec == UTIME_NOW) {
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        return now;
    }
    return wanted;
}

}  // namespace

WindowsFs::WindowsFs(SessionPool& pool, uid_t uid, gid_t gid) : pool_(pool), uid_(uid), gid_(gid) {}

template<class Fn>
int WindowsFs::on_path(Fn&& fn, bool retry) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        SessionPool::Lease lease = pool_.acquire();
        if (!lease.ok()) {
            return -SmbErrno::map(lease.error());
        }
        const int r = fn(lease.session());
        if (r < 0 && SmbErrno::connection_lost(-r)) {
            lease.discard();
            if (retry && attempt == 0) {
                continue;
            }
        }
        return r < 0 ? -SmbErrno::map(-r) : r;
    }
    return -EIO;
}

template<class Fn>
std::int64_t WindowsFs::on_handle(std::uint64_t fh, Fn&& fn) {
    const auto entry = files_.find(fh);
    if (!entry) {
        return -EBADF;
    }
    SessionPool::Lease lease = pool_.acquire_slot(entry->slot, entry->generation);
    if (!lease.ok()) {
        return -EIO;
    }
    const std::int64_t r = fn(lease.session(), entry->handle);
    if (r < 0 && SmbErrno::connection_lost(static_cast<int>(-r))) {
        lease.discard();
    }
    return r < 0 ? -SmbErrno::map(static_cast<int>(-r)) : r;
}

int WindowsFs::getattr(const std::string& path, struct stat& st) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    SmbAttr attr;
    const int r = on_path([&](SmbSession& s) { return s.stat(path, attr); }, true);
    if (r == 0) {
        SmbAttrConvert::to_stat(attr, uid_, gid_, st);
    }
    return r;
}

int WindowsFs::readdir(const std::string& path, const DirFill& fill) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    std::vector<SmbDirEntry> entries;
    const int r = on_path(
        [&](SmbSession& s) {
            entries.clear();
            return s.list(path, entries);
        },
        true);
    if (r != 0) {
        return r;
    }
    fill(".", nullptr);
    fill("..", nullptr);
    for (const SmbDirEntry& entry : entries) {
        if (entry.name.empty() || entry.name == "." || entry.name == "..") {
            continue;
        }
        struct stat st;
        SmbAttrConvert::to_stat(entry.attr, uid_, gid_, st);
        fill(entry.name, &st);
    }
    return 0;
}

int WindowsFs::open_with(const std::string& path, int flags, std::uint64_t& fh) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    SessionPool::Lease lease = pool_.acquire();
    if (!lease.ok()) {
        return -SmbErrno::map(lease.error());
    }
    SmbSession::Handle handle = 0;
    const int r = lease.session().open(path, flags & kKeptOpenFlags, handle);
    if (r < 0) {
        if (SmbErrno::connection_lost(-r)) {
            lease.discard();
        }
        return -SmbErrno::map(-r);
    }
    fh = files_.add({lease.slot(), lease.generation(), handle}, path, flags & O_ACCMODE);
    return 0;
}

int WindowsFs::open(const std::string& path, int flags, std::uint64_t& fh) {
    return open_with(path, flags & ~O_CREAT, fh);
}

int WindowsFs::create(const std::string& path, int flags, std::uint64_t& fh) {
    return open_with(path, flags | O_CREAT, fh);
}

std::int64_t WindowsFs::read(std::uint64_t fh, std::uint64_t offset, char* buffer,
                             std::size_t size) {
    return striped(fh, offset, buffer, size, false);
}

std::int64_t WindowsFs::write(std::uint64_t fh, std::uint64_t offset, const char* buffer,
                              std::size_t size) {
    return striped(fh, offset, const_cast<char*>(buffer), size, true);
}

std::int64_t WindowsFs::transfer(SmbSession& session, SmbSession::Handle handle,
                                 std::uint64_t offset, char* buffer, std::size_t size,
                                 bool writing) {
    return writing ? session.write(handle, offset, buffer, size)
                   : session.read(handle, offset, buffer, size);
}

std::int64_t WindowsFs::striped(std::uint64_t fh, std::uint64_t offset, char* buffer,
                                std::size_t size, bool writing) {
    const auto file = files_.file(fh);
    if (!file) {
        return -EBADF;
    }
    if (size < kStripeMin || !file->striping) {
        return on_handle(fh, [&](SmbSession& s, SmbSession::Handle h) {
            return transfer(s, h, offset, buffer, size, writing);
        });
    }
    std::size_t part = (size + kStripes - 1) / kStripes;
    part = (part + kStripeAlign - 1) / kStripeAlign * kStripeAlign;
    std::vector<std::future<std::int64_t>> helpers;
    std::vector<std::size_t> lengths;
    std::size_t at = part;
    for (std::size_t lane = 0; at < size; ++lane, at += part) {
        const std::size_t length = std::min(part, size - at);
        lengths.push_back(length);
        helpers.push_back(
            std::async(std::launch::async, [this, file, lane, offset, buffer, at, length, writing] {
                return transfer_part(*file, lane, offset + at, buffer + at, length, writing);
            }));
    }
    const std::size_t first_length = std::min(part, size);
    std::vector<std::int64_t> results;
    results.push_back(on_handle(fh, [&](SmbSession& s, SmbSession::Handle h) {
        return transfer(s, h, offset, buffer, first_length, writing);
    }));
    for (auto& helper : helpers) {
        results.push_back(helper.get());
    }
    std::int64_t total = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const std::size_t length = i == 0 ? first_length : lengths[i - 1];
        if (results[i] < 0) {
            return total > 0 ? total : results[i];
        }
        total += results[i];
        if (static_cast<std::size_t>(results[i]) < length) {
            break;
        }
    }
    return total;
}

std::int64_t WindowsFs::transfer_part(FileTable::File& file, std::size_t lane, std::uint64_t offset,
                                      char* buffer, std::size_t size, bool writing) {
    SessionPool::Lease lease;
    FileTable::Entry entry;
    {
        const std::lock_guard lock{file.mutex};
        if (lane < file.lanes.size()) {
            entry = file.lanes[lane];
            lease = pool_.acquire_slot(entry.slot, entry.generation);
            if (!lease.ok()) {
                file.lanes[lane] = {};
            }
        }
        if (!lease.ok()) {
            std::vector<std::size_t> used{file.primary.slot};
            for (const FileTable::Entry& other : file.lanes) {
                used.push_back(other.slot);
            }
            lease = pool_.try_acquire(used);
            if (lease.ok()) {
                SmbSession::Handle handle = 0;
                if (lease.session().open(file.path, file.flags & O_ACCMODE, handle) < 0) {
                    file.striping = false;
                    lease = {};
                } else {
                    entry = {lease.slot(), lease.generation(), handle};
                    if (lane >= file.lanes.size()) {
                        file.lanes.resize(lane + 1);
                    }
                    file.lanes[lane] = entry;
                }
            }
        }
    }
    if (!lease.ok()) {
        SessionPool::Lease primary = pool_.acquire_slot(file.primary.slot, file.primary.generation);
        if (!primary.ok()) {
            return -EIO;
        }
        const std::int64_t r =
            transfer(primary.session(), file.primary.handle, offset, buffer, size, writing);
        return r < 0 ? -SmbErrno::map(static_cast<int>(-r)) : r;
    }
    const std::int64_t r = transfer(lease.session(), entry.handle, offset, buffer, size, writing);
    if (r < 0 && SmbErrno::connection_lost(static_cast<int>(-r))) {
        lease.discard();
    }
    return r < 0 ? -SmbErrno::map(static_cast<int>(-r)) : r;
}

int WindowsFs::release(std::uint64_t fh) {
    const auto file = files_.remove(fh);
    if (!file) {
        return 0;
    }
    std::vector<FileTable::Entry> handles = file->lanes;
    handles.push_back(file->primary);
    for (const FileTable::Entry& entry : handles) {
        if (entry.generation == 0) {
            continue;
        }
        SessionPool::Lease lease = pool_.acquire_slot(entry.slot, entry.generation);
        if (lease.ok() && lease.session().close(entry.handle) < 0) {
            lease.discard();
        }
    }
    return 0;
}

int WindowsFs::truncate(const std::string& path, std::uint64_t size) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) {
        SmbSession::Handle h = 0;
        if (const int r = s.open(path, O_WRONLY, h); r < 0) {
            return r;
        }
        const int r = s.truncate(h, size);
        s.close(h);
        return r;
    });
}

int WindowsFs::ftruncate(std::uint64_t fh, std::uint64_t size) {
    return static_cast<int>(on_handle(fh, [&](SmbSession& s, SmbSession::Handle h) -> std::int64_t {
        return s.truncate(h, size);
    }));
}

int WindowsFs::unlink(const std::string& path) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) { return s.unlink(path); });
}

int WindowsFs::rename(const std::string& from, const std::string& to, bool no_replace) {
    if (const int r = SmbPath::check(from); r != 0) {
        return r;
    }
    if (const int r = SmbPath::check(to); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) {
        int r = s.rename(from, to);
        if (r >= 0 || no_replace || (r != -EEXIST && r != -EACCES)) {
            return r;
        }
        SmbAttr source;
        SmbAttr target;
        if (s.stat(from, source) < 0 || s.stat(to, target) < 0 || source.directory ||
            target.directory) {
            return r;
        }
        if (const int u = s.unlink(to); u < 0) {
            return u;
        }
        return s.rename(from, to);
    });
}

int WindowsFs::mkdir(const std::string& path) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) { return s.mkdir(path); });
}

int WindowsFs::rmdir(const std::string& path) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) { return s.rmdir(path); });
}

int WindowsFs::utimens(const std::string& path, const timespec times[2]) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    return on_path([&](SmbSession& s) {
        timespec atime = times[0];
        timespec mtime = times[1];
        if (atime.tv_nsec == UTIME_OMIT || mtime.tv_nsec == UTIME_OMIT) {
            SmbAttr current;
            if (const int r = s.stat(path, current); r < 0) {
                return r;
            }
            atime = resolve(atime, current.atime);
            mtime = resolve(mtime, current.mtime);
        } else {
            atime = resolve(atime, {});
            mtime = resolve(mtime, {});
        }
        return s.utimens(path, atime, mtime);
    });
}

int WindowsFs::statfs(const std::string& path, struct statvfs& out) {
    if (const int r = SmbPath::check(path); r != 0) {
        return r;
    }
    const int r = on_path([&](SmbSession& s) { return s.statvfs(path, out); }, true);
    if (r == -EIO) {
        return r;
    }
    if (r < 0) {
        out = {};
        out.f_bsize = 4096;
        out.f_frsize = 4096;
        out.f_blocks = 1ULL << 28;
        out.f_bfree = 1ULL << 27;
        out.f_bavail = 1ULL << 27;
        out.f_namemax = 255;
        return 0;
    }
    return 0;
}

}  // namespace lsw::host::platform
