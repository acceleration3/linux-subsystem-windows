#include "lsw/protocol/endpoint_platform.hpp"

#include "lsw/protocol/fd_frame_channel.hpp"
#include "lsw/protocol/shmem_endpoint.hpp"
#include "lsw/protocol/unix_endpoint.hpp"

#include <cerrno>
#include <dirent.h>
#include <utility>

namespace lsw::protocol {

namespace {

void set_error(std::string* error_out, std::string msg) {
    if (error_out != nullptr) {
        *error_out = std::move(msg);
    }
}

}  // namespace

// The host's schemes.
//
// ``unix://`` is the inspectable one — a socket a debugger, an strace or
// a packet dump can be pointed at, which is exactly what you want in
// hand while chasing a race in the one you cannot. That is why it is
// kept rather than deleted now that shared memory works.
//
// ``shm://`` is one end of the VM path, not a simulation of it: the
// manager creates POSIX shared memory and hands the fd to QEMU, so the
// host half of a guest connection is this same PosixShmRegion.
std::unique_ptr<FrameChannel> platform_open_channel(std::string_view scheme,
                                                    const std::string& target,
                                                    std::string* error_out) {
    if (scheme == "unix") {
        const int fd = connect_unix_socket(target);
        if (fd < 0) {
            set_error(error_out, "connect to " + target + " failed");
            return nullptr;
        }
        auto channel = std::make_unique<FdFrameChannel>(fd);
        channel->set_description("unix://" + target);
        return channel;
    }
    if (scheme == "shm") {
        auto channel = attach_shmem_channel(target);
        if (channel == nullptr) {
            set_error(error_out, "no readable LSW region at shm://" + target);
            return nullptr;
        }
        return channel;
    }
    set_error(error_out, "unsupported transport scheme: " + std::string{scheme});
    return nullptr;
}

std::unique_ptr<Listener> platform_open_listener(std::string_view scheme,
                                                 const std::string& target,
                                                 std::string* error_out) {
    if (scheme == "unix") {
        auto l = std::make_unique<UnixListener>();
        if (const int rc = l->bind(target); rc != 0) {
            set_error(error_out, "bind to " + target + " failed");
            return nullptr;
        }
        return l;
    }
    if (scheme == "shm") {
        auto l = std::make_unique<ShmemListener>();
        if (const int rc = l->create(target); rc != 0) {
            set_error(error_out, "could not create region shm://" + target);
            return nullptr;
        }
        return l;
    }
    if (scheme == "shm-attach") {
        // Serve a region somebody else owns — the manager's, for a VM.
        auto l = std::make_unique<ShmemListener>();
        const int rc = l->attach(target);
        if (rc == 0) {
            return l;
        }
        std::string why = rc == -EPROTO
                              ? "exists but is not an LSW region of this version "
                                "(manager and render server are different builds?)"
                              : "does not exist";
        // Naming the candidates saves the next person guessing a UUID.
        std::string candidates;
        if (DIR* d = ::opendir("/dev/shm"); d != nullptr) {
            while (dirent* e = ::readdir(d)) {
                const std::string n{e->d_name};
                if (n.rfind("lsw-", 0) == 0) {
                    candidates += (candidates.empty() ? "" : ", ") + n;
                }
            }
            ::closedir(d);
        }
        set_error(error_out, "shm-attach://" + target + " " + why
                                 + (candidates.empty()
                                        ? "; no LSW regions are present (is the VM running "
                                          "under a live manager?)"
                                        : "; present regions: " + candidates));
        return nullptr;
    }
    set_error(error_out, "unsupported listener scheme: " + std::string{scheme});
    return nullptr;
}

}  // namespace lsw::protocol
