#include "fake_smb_backend.hpp"
#include "lsw/host/platform/session_pool.hpp"
#include "lsw/host/platform/smb_attr_convert.hpp"
#include "lsw/host/platform/smb_errno.hpp"
#include "lsw/host/platform/smb_path.hpp"
#include "lsw/host/platform/windows_fs.hpp"

#include <cstdio>
#include <map>
#include <sys/stat.h>
#include <thread>
#include <vector>

using namespace lsw::host::platform;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

struct Rig {
    fake::Store store;
    fake::Backend backend{store};
    SessionPool pool;
    WindowsFs fs;

    explicit Rig(std::size_t sessions = 4) : pool(backend, sessions), fs(pool, 1000, 100) {}
};

void paths() {
    check(SmbPath::check("/") == 0 && SmbPath::check("/Windows/System32/cmd.exe") == 0,
          "ordinary paths pass");
    check(SmbPath::check("") == -EINVAL && SmbPath::check("a/b") == -EINVAL,
          "a path must start with a slash");
    check(SmbPath::check("/a:b") == -EINVAL && SmbPath::check("/a\\b") == -EINVAL &&
              SmbPath::check("/a*") == -EINVAL && SmbPath::check("/a?") == -EINVAL &&
              SmbPath::check("/a|b") == -EINVAL && SmbPath::check("/a\"b") == -EINVAL &&
              SmbPath::check("/a<b>") == -EINVAL,
          "names Windows cannot hold are refused");
    check(SmbPath::check("/dir/name.") == -EINVAL && SmbPath::check("/dir/name ") == -EINVAL,
          "a trailing dot or space is refused");
    check(SmbPath::check("/" + std::string(300, 'a')) == -ENAMETOOLONG,
          "a name over 255 bytes is too long");
    check(SmbPath::check("/a/../b") == -EINVAL, "dot dot is refused");
    check(SmbPath::url("smb://h:1/C$", "/a b/100%.txt") == "smb://h:1/C$/a b/100%25.txt",
          "a percent sign is escaped in the URL");
    check(SmbPath::url("smb://h/C$", "/") == "smb://h/C$/", "the root keeps its slash");
}

void errors() {
    check(SmbErrno::map(ECONNRESET) == EIO && SmbErrno::map(ETIMEDOUT) == EIO &&
              SmbErrno::map(EPIPE) == EIO,
          "connection failures are EIO");
    check(SmbErrno::map(ENOENT) == ENOENT && SmbErrno::map(EACCES) == EACCES &&
              SmbErrno::map(ENOTEMPTY) == ENOTEMPTY && SmbErrno::map(EEXIST) == EEXIST,
          "file errors pass through");
    check(SmbErrno::map(0) == EIO && SmbErrno::map(-ENOENT) == ENOENT,
          "zero is EIO and the sign is ignored");
    check(SmbErrno::connection_lost(ECONNRESET) && !SmbErrno::connection_lost(ENOENT),
          "only connection errors count as lost");
}

void attributes() {
    SmbAttr dir;
    dir.directory = true;
    dir.mtime = {42, 7};
    struct stat st;
    SmbAttrConvert::to_stat(dir, 1000, 100, st);
    check(S_ISDIR(st.st_mode) && (st.st_mode & 07777) == 0755 && st.st_uid == 1000 &&
              st.st_gid == 100 && st.st_mtim.tv_sec == 42 && st.st_mtim.tv_nsec == 7,
          "a directory is 0755 owned by the user with its time");
    SmbAttr file;
    file.size = 1025;
    SmbAttrConvert::to_stat(file, 1000, 100, st);
    check(S_ISREG(st.st_mode) && (st.st_mode & 07777) == 0644 && st.st_size == 1025 &&
              st.st_blocks == 3 && st.st_nlink == 1,
          "a file is 0644 with its size and blocks");
}

void reading_and_writing() {
    Rig rig;
    rig.store.add_dir("/docs");
    rig.store.add_file("/docs/a.txt", "hello world");
    struct stat st;
    check(rig.fs.getattr("/docs/a.txt", st) == 0 && st.st_size == 11, "getattr reports the size");
    check(rig.fs.getattr("/missing", st) == -ENOENT, "a missing file is ENOENT");
    check(rig.fs.getattr("/bad:name", st) == -EINVAL, "a bad name never reaches the share");

    std::uint64_t fh = 0;
    char buf[32] = {};
    check(rig.fs.open("/docs/a.txt", O_RDONLY, fh) == 0, "open succeeds");
    check(rig.fs.read(fh, 6, buf, sizeof buf) == 5 && std::string(buf, 5) == "world",
          "read at an offset");
    check(rig.fs.read(fh, 100, buf, sizeof buf) == 0, "read past the end is empty");
    check(rig.fs.release(fh) == 0 && rig.fs.open_files() == 0 && rig.store.closes == 1,
          "release closes it");
    check(rig.fs.read(fh, 0, buf, 1) == -EBADF, "a released handle is bad");

    check(rig.fs.create("/docs/b.txt", O_WRONLY | O_EXCL, fh) == 0, "create makes a file");
    check(rig.fs.write(fh, 0, "abcdef", 6) == 6 && rig.fs.write(fh, 3, "XY", 2) == 2,
          "writes at offsets");
    check(rig.fs.ftruncate(fh, 4) == 0, "ftruncate shortens");
    rig.fs.release(fh);
    check(rig.store.content("/docs/b.txt") == "abcX", "the share holds what was written");
    check(rig.fs.create("/docs/b.txt", O_WRONLY | O_EXCL, fh) == -EEXIST,
          "exclusive create of an existing file fails");
    check(rig.fs.truncate("/docs/b.txt", 8) == 0 && rig.store.content("/docs/b.txt").size() == 8,
          "truncate by path grows");
    check(rig.fs.open("/docs/a.txt", O_WRONLY | O_TRUNC, fh) == 0 &&
              rig.store.content("/docs/a.txt").empty(),
          "O_TRUNC empties");
    rig.fs.release(fh);
}

void names() {
    Rig rig;
    rig.store.add_file("/old", "1");
    rig.store.add_file("/new", "2");
    check(rig.fs.rename("/old", "/new", false) == 0 && rig.store.content("/new") == "1" &&
              !rig.store.exists("/old"),
          "rename replaces an existing file");
    rig.store.add_file("/x", "x");
    check(rig.fs.rename("/x", "/new", true) == -EEXIST, "no-replace rename refuses");
    check(rig.fs.mkdir("/d") == 0 && rig.fs.mkdir("/d") == -EEXIST, "mkdir");
    rig.store.add_file("/d/f", "");
    check(rig.fs.rmdir("/d") == -ENOTEMPTY, "rmdir of a full folder");
    check(rig.fs.unlink("/d/f") == 0 && rig.fs.rmdir("/d") == 0, "unlink then rmdir");
    timespec times[2] = {{5, 0}, {1234, 0}};
    check(rig.fs.utimens("/new", times) == 0, "utimens");
    SmbAttr attr;
    int e = 0;
    auto session = rig.backend.connect(e);
    check(session->stat("/new", attr) == 0 && attr.mtime.tv_sec == 1234,
          "the time reached the share");
    times[0] = {0, UTIME_OMIT};
    times[1] = {0, UTIME_OMIT};
    check(rig.fs.utimens("/new", times) == 0 &&
              (session->stat("/new", attr), attr.mtime.tv_sec == 1234),
          "UTIME_OMIT keeps the time");
    struct statvfs sv;
    check(rig.fs.statfs("/", sv) == 0 && sv.f_blocks == 1000, "statfs");
}

void directories() {
    Rig rig;
    rig.store.add_dir("/dir");
    for (int i = 0; i < 50; ++i) {
        rig.store.add_file("/dir/f" + std::to_string(i),
                           std::string(static_cast<std::size_t>(i), 'x'));
    }
    std::map<std::string, struct stat> seen;
    int dots = 0;
    int without_attr = 0;
    const int r = rig.fs.readdir("/dir", [&](const std::string& name, const struct stat* st) {
        if (name == "." || name == "..") {
            ++dots;
            return;
        }
        if (st == nullptr) {
            ++without_attr;
            return;
        }
        seen[name] = *st;
    });
    check(r == 0 && seen.size() == 50 && dots == 2 && without_attr == 0,
          "readdir gives dot, dot dot once and every entry with its attributes");
    check(seen["f7"].st_size == 7 && S_ISREG(seen["f7"].st_mode) && seen["f7"].st_uid == 1000,
          "the attributes come from the listing");
    check(rig.fs.readdir("/nope", [](const std::string&, const struct stat*) {}) == -ENOENT,
          "a missing folder");
}

void reconnecting() {
    Rig rig;
    rig.store.add_file("/a", "data");
    struct stat st;
    check(rig.fs.getattr("/a", st) == 0 && rig.store.connects == 1, "the first call connects");
    rig.store.break_next = 1;
    check(rig.fs.getattr("/a", st) == 0 && rig.store.connects == 2,
          "a dropped connection is replaced and the read retried");
    rig.store.break_next = 2;
    check(rig.fs.getattr("/a", st) == -EIO, "two failures in a row are EIO");
    check(rig.fs.getattr("/a", st) == 0, "and the next call works again");
    rig.store.break_next = 1;
    check(rig.fs.unlink("/a") == -EIO && rig.store.exists("/a"),
          "a write that hit a dropped connection is not repeated");
    rig.store.fail_connect = ECONNREFUSED;
    SessionPool pool{rig.backend, 1};
    WindowsFs fs{pool, 1, 1};
    check(fs.getattr("/a", st) == -EIO, "a refused connection is EIO");
}

void affinity() {
    Rig rig{3};
    rig.store.add_file("/a", "aaaa");
    rig.store.add_file("/b", "bbbb");
    std::uint64_t fa = 0;
    std::uint64_t fb = 0;
    check(rig.fs.open("/a", O_RDONLY, fa) == 0 && rig.fs.open("/b", O_RDONLY, fb) == 0,
          "two files open");
    char buf[8];
    struct stat st;
    for (int i = 0; i < 20; ++i) {
        rig.fs.getattr("/a", st);
        if (rig.fs.read(fa, 0, buf, 4) != 4 || std::string(buf, 4) != "aaaa") {
            check(false, "a handle stays on the connection that opened it");
            return;
        }
        if (rig.fs.read(fb, 0, buf, 4) != 4 || std::string(buf, 4) != "bbbb") {
            check(false, "the other handle too");
            return;
        }
    }
    check(true, "handles stay on their own connections across other traffic");
    rig.store.break_next = 1;
    check(rig.fs.read(fa, 0, buf, 4) == -EIO, "a lost connection fails the handle with EIO");
    check(rig.fs.read(fa, 0, buf, 4) == -EIO, "and it stays failed");
    check(rig.fs.release(fa) == 0 && rig.fs.release(fb) == 0, "releasing is still fine");
}

void striping() {
    Rig rig{4};
    std::uint64_t fh = 0;
    check(rig.fs.create("/big", O_WRONLY, fh) == 0, "create for a large write");
    std::string data(1 << 20, 'q');
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>('a' + (i / 4096) % 26);
    }
    check(rig.fs.write(fh, 0, data.data(), data.size()) == static_cast<std::int64_t>(data.size()),
          "a 1 MiB write reports all of it");
    check(rig.fs.write(fh, data.size(), data.data(), data.size()) ==
              static_cast<std::int64_t>(data.size()),
          "and a second one after it");
    check(rig.store.opens >= 2, "the large write used a second handle on another connection");
    rig.fs.release(fh);
    check(rig.store.closes == rig.store.opens, "every handle is closed again");
    std::string back(3 << 20, '\0');
    check(rig.fs.open("/big", O_RDONLY, fh) == 0, "open it again");
    check(rig.fs.read(fh, 0, back.data(), 1 << 20) == (1 << 20) &&
              back.compare(0, 1 << 20, data) == 0,
          "a 1 MiB read comes back whole and in order");
    check(rig.fs.read(fh, (2 << 20) - 1000, back.data(), 1 << 20) == 1000,
          "a striped read that runs into the end is cut short");
    check(rig.fs.read(fh, 5 << 20, back.data(), 1 << 20) == 0,
          "a striped read past the end is empty");
    rig.fs.release(fh);
    check(rig.store.content("/big") == data + data, "the pieces land in order");
}

void pool_limits() {
    fake::Store store;
    fake::Backend backend{store};
    SessionPool pool{backend, 2};
    {
        auto one = pool.acquire();
        auto two = pool.acquire();
        check(one.ok() && two.ok() && one.slot() != two.slot() && store.connects == 2,
              "two leases use two connections");
        std::atomic<bool> got{false};
        std::thread waiter{[&] {
            auto three = pool.acquire();
            got = three.ok();
        }};
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        check(!got, "a third lease waits");
        one = {};
        waiter.join();
        check(got && store.connects == 2, "it gets the freed connection without a new one");
    }
    check(pool.connected() == 2, "connections are kept");
    {
        auto lease = pool.acquire();
        const auto generation = lease.generation();
        const auto slot = lease.slot();
        lease.discard();
        lease = {};
        auto stale = pool.acquire_slot(slot, generation);
        check(!stale.ok(), "a handle's old generation is refused after a discard");
    }
}

void parallel() {
    Rig rig{4};
    for (int i = 0; i < 8; ++i) {
        rig.store.add_file("/f" + std::to_string(i), std::string(1000, static_cast<char>('a' + i)));
    }
    std::atomic<int> bad{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            const std::string path = "/f" + std::to_string(t);
            for (int round = 0; round < 200; ++round) {
                std::uint64_t fh = 0;
                char buf[1000];
                if (rig.fs.open(path, O_RDONLY, fh) != 0) {
                    ++bad;
                    continue;
                }
                if (rig.fs.read(fh, 0, buf, sizeof buf) != 1000 || buf[0] != 'a' + t) {
                    ++bad;
                }
                rig.fs.release(fh);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    check(bad == 0 && rig.store.connects <= 4 && rig.fs.open_files() == 0,
          "eight threads share four connections without mixing files up");
}

}  // namespace

int main() {
    paths();
    errors();
    attributes();
    reading_and_writing();
    names();
    directories();
    reconnecting();
    affinity();
    striping();
    pool_limits();
    parallel();
    return g_failures == 0 ? 0 : 1;
}
