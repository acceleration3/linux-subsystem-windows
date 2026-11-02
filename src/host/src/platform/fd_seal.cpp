#include "lsw/host/platform/fd_seal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace lsw::host::platform {

int FdSeal::close_on_exec() {
    std::vector<int> fds;
    DIR* dir = opendir("/proc/self/fd");
    if (dir == nullptr) {
        return 0;
    }
    const int own = dirfd(dir);
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const int fd = std::atoi(entry->d_name);
        if (fd > 2 && fd != own) {
            fds.push_back(fd);
        }
    }
    closedir(dir);

    int changed = 0;
    for (const int fd : fds) {
        const int flags = fcntl(fd, F_GETFD);
        if (flags < 0 || (flags & FD_CLOEXEC) != 0) {
            continue;
        }
        char link[64];
        std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
        char target[256];
        const ssize_t n = readlink(link, target, sizeof target - 1);
        if (n <= 0) {
            continue;
        }
        target[n] = '\0';
        if (std::strncmp(target, "socket:", 7) == 0 || std::strcmp(target, "/dev/fuse") == 0) {
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
            ++changed;
        }
    }
    return changed;
}

}  // namespace lsw::host::platform
