#include "lsw/protocol/endpoint_platform.hpp"

#include "lsw/protocol/ivshmem_region.hpp"
#include "lsw/protocol/shmem_frame_channel.hpp"

#include "lsw/linkproto/region.hpp"

#include <cerrno>
#include <memory>
#include <utility>

namespace lsw::protocol {

namespace {

void set_error(std::string* error_out, std::string msg) {
    if (error_out != nullptr) {
        *error_out = std::move(msg);
    }
}

}  // namespace

// The guest's schemes.
//
// One, eventually: ``shm://``, mapped through the IVSHMEM BAR. There is
// no ``unix://`` here and there is not meant to be — a guest has no
// socket to the host, which is the whole reason the shared-memory
// channel exists.
//
// The target part of the URL is ignored: a guest has exactly one
// IVSHMEM device and the manager decides its layout, so there is nothing
// to name. ``shm://`` alone is the whole address. It stays in the URL
// for one reason — the ICD reads the same ``LSW_SERVER_SOCKET`` here as
// it does on the host, so nothing above has to know which it is.
//
// Everything else about this function is the local path, unchanged:
// find the cmdstream channel by kind, bind a client-role
// ``ShmemFrameChannel`` to it, hand it back. Compare
// ``attach_shmem_channel`` on the POSIX side — the only difference is
// which SharedRegion is constructed, which is exactly the one row rule 1
// permits.
std::unique_ptr<FrameChannel> platform_open_channel(std::string_view scheme,
                                                    const std::string& target,
                                                    std::string* error_out) {
    if (scheme != "shm") {
        set_error(error_out, "unsupported transport scheme on Windows: " + std::string{scheme}
                                 + " (the guest has only shm://)");
        return nullptr;
    }

    auto region = std::make_shared<IvshmemRegion>();
    if (const int rc = region->open(); rc != 0) {
        switch (rc) {
        case -ENODEV:
            set_error(error_out, "no IVSHMEM device — the driver is missing, or the VM has no "
                                 "<shmem/> device");
            break;
        case -EPROTO:
            set_error(error_out, "the IVSHMEM region is not an LSW region of this version — "
                                 "manager and guest are different builds");
            break;
        case -EBUSY:
            set_error(error_out,
                      "the IVSHMEM device would not hand out a mapping (os error "
                          + std::to_string(region->last_os_error())
                          + ") — another process in this guest most likely has it, since the "
                            "driver tracks a single owner");
            break;
        case -ENXIO:
            set_error(error_out, "the IVSHMEM device would not report its size (os error "
                                     + std::to_string(region->last_os_error()) + ")");
            break;
        default:
            set_error(error_out, "could not map the IVSHMEM BAR");
            break;
        }
        return nullptr;
    }

    auto channel = std::make_unique<ShmemFrameChannel>();
    if (channel->bind_channel(std::move(region), linkproto::kChannelCmdStream,
                              ShmemFrameChannel::Role::Client)
        != 0) {
        set_error(error_out, "the region has no usable cmdstream channel" + target);
        return nullptr;
    }
    return channel;
}

// Nothing listens in the guest. The application is the client; the
// render server is on the host.
std::unique_ptr<Listener> platform_open_listener(std::string_view scheme, const std::string&,
                                                 std::string* error_out) {
    set_error(error_out, "listening is not supported on Windows (scheme " + std::string{scheme}
                             + ")");
    return nullptr;
}

}  // namespace lsw::protocol
