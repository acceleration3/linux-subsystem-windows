#include "fake_smb_backend.hpp"
#include "lsw/host/platform/fuse_mount.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace lsw::host::platform;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

std::string slurp(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

int main() {
    if (::access("/dev/fuse", R_OK | W_OK) != 0 ||
        std::system("command -v fusermount3 >/dev/null 2>&1") != 0) {
        std::printf("skip: FUSE is not available here\n");
        return 77;
    }
    char tmpl[] = "/tmp/lsw-fuse-test-XXXXXX";
    const std::string point = mkdtemp(tmpl);

    fake::Store store;
    store.add_dir("/docs");
    store.add_file("/docs/hello.txt", "hello from the share");
    store.add_file("/big.bin", std::string(3 << 20, 'z'));
    auto stack = std::make_shared<WindowsFsStack>(std::make_unique<fake::Backend>(store), 4,
                                                  ::getuid(), ::getgid());

    FuseMount::Options options;
    options.mount_point = point;
    options.fsname = "//127.0.0.1/C$";
    FuseMount mount{stack, options};
    std::string error;
    if (!mount.start(error)) {
        std::printf("skip: %s\n", error.c_str());
        rmdir(point.c_str());
        return 77;
    }
    check(mount.running(), "the session is running");

    struct stat st;
    check(::stat((point + "/docs/hello.txt").c_str(), &st) == 0 && st.st_size == 20 &&
              (st.st_mode & 0777) == 0644 && st.st_uid == ::getuid(),
          "stat through the mount");
    check(slurp(point + "/docs/hello.txt") == "hello from the share", "read through the mount");
    check(slurp(point + "/big.bin") == std::string(3 << 20, 'z'), "a 3 MiB file reads back whole");

    int count = 0;
    if (DIR* dir = opendir(point.c_str())) {
        while (readdir(dir) != nullptr) {
            ++count;
        }
        closedir(dir);
    }
    check(count == 4, "readdir lists dot, dot dot and the two entries");

    {
        std::ofstream out{point + "/docs/new.txt", std::ios::binary};
        out << "written from the host";
    }
    check(store.content("/docs/new.txt") == "written from the host",
          "a file written through the mount reaches the share");
    check(std::rename((point + "/docs/new.txt").c_str(), (point + "/docs/renamed.txt").c_str()) ==
                  0 &&
              store.exists("/docs/renamed.txt") && !store.exists("/docs/new.txt"),
          "rename");
    check(mkdir((point + "/made").c_str(), 0755) == 0 && store.exists("/made"), "mkdir");
    check(rmdir((point + "/made").c_str()) == 0 && !store.exists("/made"), "rmdir");
    check(unlink((point + "/docs/renamed.txt").c_str()) == 0 && !store.exists("/docs/renamed.txt"),
          "unlink");
    check(::stat((point + "/gone").c_str(), &st) != 0 && errno == ENOENT,
          "a missing file is ENOENT");
    check(stack->fs().open_files() == 0, "no handle is left open");

    const auto before = std::chrono::steady_clock::now();
    check(mount.stop(std::chrono::milliseconds{5000}), "stop returns once unmounted");
    const auto took = std::chrono::steady_clock::now() - before;
    check(took < std::chrono::seconds{4}, "and does not hang");
    check(!mount.running(), "the session is over");
    check(::stat((point + "/docs").c_str(), &st) != 0, "the files are gone from the folder");
    check(store.live == 0 || store.live <= 4, "connections are bounded");

    rmdir(point.c_str());
    return g_failures == 0 ? 0 : 1;
}
